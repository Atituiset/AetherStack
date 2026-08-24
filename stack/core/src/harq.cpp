#include "core/harq.h"
#include "core/pdu_trace.h"
#include "mac/mac_pdu.h"
#include "phy/fec.h"
#include <algorithm>
#include <stdexcept>

namespace core {

// ---- wire format ------------------------------------------------------------

std::vector<uint8_t> HarqHeader::pack() const {
    std::vector<uint8_t> v(kSize);
    v[0] = static_cast<uint8_t>((proc & 0x7F) | (ndi ? 0x80 : 0));
    v[1] = rv;
    v[2] = static_cast<uint8_t>(len & 0xFF);
    v[3] = static_cast<uint8_t>((len >> 8) & 0xFF);
    v[4] = static_cast<uint8_t>(info_len & 0xFF);
    v[5] = static_cast<uint8_t>((info_len >> 8) & 0xFF);
    v[6] = 0xA9; // magic: distinguishes HARQ frames from legacy payloads
    return v;
}

bool HarqHeader::unpack(const std::vector<uint8_t>& buf, HarqHeader& out) {
    if (buf.size() < kSize || buf[6] != 0xA9) return false; // magic mismatch
    out.proc = buf[0] & 0x7F;
    out.ndi = (buf[0] >> 7) & 1;
    out.rv = buf[1];
    out.len = static_cast<uint16_t>(buf[2] | (buf[3] << 8));
    out.info_len = static_cast<uint16_t>(buf[4] | (buf[5] << 8));
    return true;
}

bool is_harq_framed(const std::vector<uint8_t>& buf) {
    return buf.size() >= HarqHeader::kSize && buf[6] == 0xA9; // magic at last header byte
}

// ---- codec helpers ----------------------------------------------------------

std::vector<uint8_t> link_encode(const std::vector<uint8_t>& mac_pdu,
                                 uint8_t proc, uint8_t ndi) {
    auto block = phy::crc_attach(mac_pdu);
    auto bits = phy::bytes_to_bits_fec(block);
    auto coded_bits = phy::fec_encode(bits);
    // Byte-alignment padding for the wire; the receiver drops it using
    // info_len before Viterbi decoding.
    while (coded_bits.size() % 8 != 0) coded_bits.push_back(0);
    auto coded_bytes = phy::bits_to_bytes_fec(coded_bits);

    HarqHeader h;
    h.proc = proc;
    h.ndi = ndi;
    h.rv = 0;
    h.len = static_cast<uint16_t>(coded_bytes.size());
    h.info_len = static_cast<uint16_t>(block.size());
    std::vector<uint8_t> payload = h.pack();
    payload.insert(payload.end(), coded_bytes.begin(), coded_bytes.end());
    return payload;
}

std::vector<uint8_t> link_payload_bits(
    const std::vector<uint8_t>& frame_payload) {
    HarqHeader h;
    if (!HarqHeader::unpack(frame_payload, h)) return {};
    std::vector<uint8_t> bytes(frame_payload.begin() + HarqHeader::kSize,
                               frame_payload.end());
    return phy::bytes_to_bits_fec(bytes);
}

bool link_decode_bits(const std::vector<uint8_t>& coded_bits,
                      size_t info_len, std::vector<uint8_t>& mac_pdu) {
    try {
        // fec_decode already strips the K-1 flush bits.
        auto info = phy::fec_decode(coded_bits);
        if (info.size() != info_len * 8) return false;
        auto block = phy::bits_to_bytes_fec(info);
        return phy::crc_verify_strip(block, mac_pdu);
    } catch (const std::invalid_argument&) {
        return false;
    }
}

// ---- TX ---------------------------------------------------------------------

HarqTx::HarqTx(const HarqTxConfig& cfg) : cfg_(cfg) {
    procs_.resize(cfg_.num_processes);
}

std::optional<HarqTx::Event> HarqTx::send(
    const std::vector<uint8_t>& mac_pdu) {
    // find a free process, round-robin from next_proc_
    for (uint8_t i = 0; i < cfg_.num_processes; ++i) {
        uint8_t p = static_cast<uint8_t>((next_proc_ + i) % cfg_.num_processes);
        Proc& pr = procs_[p];
        if (pr.busy) continue;

        pr.busy = true;
        pr.ndi ^= 1;              // toggle per new transport block
        pr.retx_count = 0;
        pr.deadline = last_now_ + cfg_.ack_timeout_ms;
        pr.crc_block = mac_pdu;

        Event ev;
        ev.type = Event::NEW_TX;
        ev.proc = p;
        ev.ndi = pr.ndi;
        ev.coded = link_encode(mac_pdu, p, pr.ndi);
        pr.coded = ev.coded;

        next_proc_ = static_cast<uint8_t>((p + 1) % cfg_.num_processes);
        return ev;
    }
    return std::nullopt; // all processes busy
}

void HarqTx::retx(uint8_t proc, const char* reason,
                  std::vector<Event>* sink) {
    Proc& pr = procs_[proc];
    if (!pr.busy) return;
    if (pr.retx_count >= cfg_.max_retx) {
        // Give up on this transport block: upper layers account for the loss.
        LOG_WARN(ev::HARQ_DROP, {{"proc", std::to_string(proc)},
                                  {"attempts",
                                   std::to_string(pr.retx_count + 1)}});
        pr.busy = false;
        pr.crc_block.clear();
        return;
    }
    ++pr.retx_count;
    pr.deadline = last_now_ + cfg_.ack_timeout_ms;
    LOG_INFO(ev::HARQ_RETX, {{"proc", std::to_string(proc)},
                              {"attempt", std::to_string(pr.retx_count)},
                              {"reason", reason}});
    Event e;
    e.type = Event::RETX;
    e.proc = proc;
    e.ndi = pr.ndi; // same NDI: receiver merges with its soft buffer
    e.coded = pr.coded;
    sink->push_back(std::move(e));
}

void HarqTx::advance(uint32_t now_ms) {
    last_now_ = std::max(last_now_, now_ms);
}

std::vector<HarqTx::Event> HarqTx::poll_timeouts(uint32_t now_ms) {
    advance(now_ms);
    std::vector<Event> out;
    for (uint8_t p = 0; p < procs_.size(); ++p) {
        if (!procs_[p].busy) continue;
        if (static_cast<int32_t>(now_ms - procs_[p].deadline) >= 0) {
            retx(p, "timeout", &out);
        }
    }
    return out;
}

void HarqTx::reset() {
    for (auto& pr : procs_) {
        pr.busy = false;
        pr.retx_count = 0;
        pr.coded.clear();
        pr.crc_block.clear();
    }
    next_proc_ = 0;
}

void HarqTx::on_ack(uint8_t proc) {
    if (proc >= procs_.size()) return;
    procs_[proc].busy = false;
    procs_[proc].crc_block.clear();
}

std::optional<HarqTx::Event> HarqTx::on_nack(uint8_t proc) {
    if (proc >= procs_.size()) return std::nullopt;
    std::vector<Event> evs;
    retx(proc, "nack", &evs);
    if (evs.empty()) return std::nullopt;
    return std::move(evs[0]);
}

// ---- RX ---------------------------------------------------------------------

void HarqRx::reset() {
    soft_.clear();
    soft_.resize(8); // stable per-proc slots; empty bits = nothing saved
}

HarqRx::Result HarqRx::receive(const std::vector<uint8_t>& frame_payload) {
    Result res;
    HarqHeader h;
    if (!HarqHeader::unpack(frame_payload, h)) return res;
    if (frame_payload.size() < HarqHeader::kSize + h.len) return res;

    std::vector<uint8_t> bytes(frame_payload.begin() + HarqHeader::kSize,
                               frame_payload.begin() + HarqHeader::kSize +
                                   h.len);
    auto rx_bits = phy::bytes_to_bits_fec(bytes);

    // A retransmission (ndi unchanged) may chase-combine with stored bits.
    bool combined = false;
    SoftBuf* slot = (h.proc < soft_.size()) ? &soft_[h.proc] : nullptr;
    size_t valid = std::min(valid_coded_bits(h), rx_bits.size());
    if (!h.ndi && slot && !slot->bits.empty() &&
        slot->bits.size() == rx_bits.size()) {
        LOG_INFO(ev::HARQ_COMBINE, {{"proc", std::to_string(h.proc)}});
        for (size_t i = 0; i < valid; ++i) {
            rx_bits[i] = (rx_bits[i] + slot->bits[i] >= 2) ? 1 : 0;
        }
        combined = true;
    }

    rx_bits.resize(std::min(rx_bits.size(), valid_coded_bits(h)));

    res.proc = h.proc;
    res.need_feedback = true; // unicasts are acknowledged
    res.delivered = link_decode_bits(rx_bits, h.info_len, res.mac_pdu);
    res.ack = res.delivered;

    // Soft-memory lifecycle:
    //   * new transport block (ndi=1): discard any stale memory, then save
    //     the bits if decoding failed (they are the base for combining).
    //   * retx (ndi=0): keep the (possibly combined) bits while failing.
    if (h.ndi || res.delivered) {
        if (slot) slot->bits.clear();
    }
    if (!res.delivered) {
        if (h.proc >= soft_.size()) soft_.resize(h.proc + 1);
        soft_[h.proc].ndi = h.ndi;
        soft_[h.proc].bits = std::move(rx_bits);
    }
    (void)combined;
    return res;
}

}

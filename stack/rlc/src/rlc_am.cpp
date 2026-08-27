#include "rlc/rlc_am.h"
#include "common/logger.h"

namespace rlc {

namespace {
constexpr size_t kMaxHoles = 256;
constexpr size_t kMaxReorder = 512;

void put_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
uint16_t get_u16(const std::vector<uint8_t>& v, size_t off) {
    return static_cast<uint16_t>(v[off] | (v[off + 1] << 8));
}
} // namespace

// ---- AmTx -------------------------------------------------------------------

std::vector<uint8_t> AmTx::build_pdu(uint16_t seq, uint8_t fi_with_poll,
                                     const std::vector<uint8_t>& payload) const {
    std::vector<uint8_t> pdu;
    pdu.reserve(payload.size() + 3);
    pdu.push_back(static_cast<uint8_t>(kAmDcData | fi_with_poll));
    put_u16(pdu, seq);
    pdu.insert(pdu.end(), payload.begin(), payload.end());
    return pdu;
}

std::vector<std::vector<uint8_t>> AmTx::tx(uint32_t now_ms,
                                           const std::vector<uint8_t>& sdu,
                                           bool force) {
    std::vector<std::vector<uint8_t>> out;
    if (sdu.empty()) return out;
    // Backpressure: refuse the new SDU while the window is full. Shedding
    // the oldest unacked PDU instead would leave the peer's reassembly
    // waiting for an SN that will never be retransmitted — a permanent
    // receive-side wedge (observed in the M16 media stress runs).
    // One-shot control SDUs (force) get a small reserve so a congested
    // media stream cannot starve call hangups or text messages.
    constexpr size_t kControlReserve = 4;
    const size_t limit = cfg_.tx_buffer_limit + (force ? kControlReserve : 0);
    if (tx_buffer_.size() >= limit) {
        ++tx_dropped_;
        return out;
    }
    last_progress_ms_ = now_ms;

    const size_t maxp = cfg_.max_pdu_payload;
    const size_t segments = (sdu.size() + maxp - 1) / maxp;
    for (size_t seg = 0; seg < segments; ++seg) {
        auto begin = sdu.begin() + static_cast<long>(seg * maxp);
        auto end = seg == segments - 1
                       ? sdu.end()
                       : sdu.begin() + static_cast<long>((seg + 1) * maxp);

        // FI: 00 complete, 01 first, 10 last, 11 middle (bits b5..b4).
        uint8_t fi = 0x00;
        if (segments > 1) {
            fi = seg == 0 ? 0x10 : (seg == segments - 1 ? 0x20 : 0x30);
        }

        // Poll periodically so a silently lost PDU still triggers a STATUS.
        bool poll = ++since_poll_ >= cfg_.poll_every;
        if (poll) since_poll_ = 0;

        std::vector<uint8_t> payload(begin, end);
        auto pdu = build_pdu(next_sn_, poll ? (fi | kAmPollBit) : fi, payload);
        tx_buffer_[next_sn_] = pdu; // held until the cumulative ACK covers it
        ++next_sn_;
        out.push_back(std::move(pdu));
    }
    return out;
}

void AmTx::reset() {
    tx_buffer_.clear();
    next_sn_ = 0;
    since_poll_ = 0;
    last_progress_ms_ = -1;
    last_nack_burst_ms_ = -1;
}

std::vector<std::vector<uint8_t>> AmTx::tick(uint32_t now_ms) {
    if (cfg_.t_poll_ms == 0 || tx_buffer_.empty()) return {};
    const int64_t now = static_cast<int64_t>(now_ms);
    if (last_progress_ms_ >= 0 &&
        now - last_progress_ms_ < static_cast<int64_t>(cfg_.t_poll_ms)) {
        return {};
    }
    // The peer's STATUS may have died on the way: blind-resend the oldest
    // unacked PDUs, bounded so a big backlog cannot flood the lower layer.
    // Duplicate/stale handling makes this safe.
    last_progress_ms_ = now;
    std::vector<std::vector<uint8_t>> out;
    for (auto& [sn, pdu] : tx_buffer_) {
        if (out.size() >= cfg_.probe_batch) break;
        ++retxs_;
        LOG_INFO(ev::RLC_AM_RETX, {{"sn", std::to_string(sn)}});
        out.push_back(pdu);
    }
    return out;
}

std::vector<std::vector<uint8_t>> AmTx::on_status(
    uint32_t now_ms, const std::vector<uint8_t>& status) {
    std::vector<std::vector<uint8_t>> retx;
    if (status.size() < 4 || status[0] != kAmDcControl) return retx;

    const uint16_t ack_sn = get_u16(status, 1);
    for (auto it = tx_buffer_.begin(); it != tx_buffer_.end();) {
        if (it->first < ack_sn) it = tx_buffer_.erase(it); // cumulatively acked
        else ++it;
    }
    const size_t count = status[3];
    const int64_t now = static_cast<int64_t>(now_ms);
    const bool burst_allowed =
        last_nack_burst_ms_ < 0 ||
        now - last_nack_burst_ms_ >= static_cast<int64_t>(cfg_.t_poll_ms);
    if (burst_allowed && count > 0) {
        last_nack_burst_ms_ = now;
        size_t sent = 0;
        for (size_t i = 0; i < count && 4 + 2 * i + 1 < status.size(); ++i) {
            if (sent >= cfg_.probe_batch) break;
            uint16_t nack = get_u16(status, 4 + 2 * i);
            auto it = tx_buffer_.find(nack);
            if (it != tx_buffer_.end()) {
                ++sent;
                ++retxs_;
                LOG_INFO(ev::RLC_AM_RETX, {{"sn", std::to_string(nack)}});
                retx.push_back(it->second); // buffer keeps the original
            }
        }
    }
    last_progress_ms_ = now_ms; // peer is alive: restart the probe window
    return retx;
}

// ---- AmRx -------------------------------------------------------------------

void AmRx::accept_in_order(uint16_t sn, uint8_t fi,
                           const std::vector<uint8_t>& payload, Outcome& out) {
    holes_.erase(sn); // a nacked SN just arrived (or was never a hole)
    ++accepts_;
    switch (fi & kAmFiMask) {
        case 0x00: // complete SDU
            out.delivered.push_back(payload);
            break;
        case 0x10: // first segment
            collecting_ = true;
            partial_ = payload;
            break;
        case 0x20: // last segment
            if (collecting_) {
                partial_.insert(partial_.end(), payload.begin(), payload.end());
                out.delivered.push_back(partial_);
                collecting_ = false;
                partial_.clear();
            }
            break;
        default: // middle segment
            if (collecting_) {
                partial_.insert(partial_.end(), payload.begin(), payload.end());
            }
            break;
    }
    ++vr_next_;
}

void AmRx::drain_reorder(Outcome& out) {
    while (!reorder_.empty()) {
        auto it = reorder_.find(vr_next_);
        if (it == reorder_.end()) break;
        accept_in_order(it->first, it->second.fi, it->second.payload, out);
        reorder_.erase(it);
    }
}

AmRx::Outcome AmRx::tick(uint32_t now_ms) {
    Outcome out;
    if (cfg_.t_reorder_ms == 0) return out;
    const int64_t now = static_cast<int64_t>(now_ms);
    // Progress = the accept counter moved since the last tick.
    if (accepts_ != tick_accepts_) {
        tick_accepts_ = accepts_;
        last_progress_ms_ = now;
    }
    if (holes_.empty() || last_progress_ms_ < 0 ||
        now - last_progress_ms_ < static_cast<int64_t>(cfg_.t_reorder_ms)) {
        return out;
    }
    // The peer's TX window has slid past these SNs (or the path is
    // persistently lossy beyond the ARQ budget): waiting longer would wedge
    // in-order delivery forever. Declare the holes lost and resynchronise —
    // the media/app layer accounts the skipped packets as loss.
    const size_t skipped = holes_.size();
    for (uint16_t h : holes_) {
        vr_next_ = static_cast<uint16_t>(h + 1); // std::set: ascending
    }
    holes_.clear();
    collecting_ = false; // any partial SDU spanned a lost segment
    partial_.clear();
    last_progress_ms_ = now; // one skip round per t_reorder_ms at most
    LOG_WARN(ev::RLC_UM_GAP_SKIP, {{"skipped", std::to_string(skipped)}});
    drain_reorder(out);
    out.status_needed = true; // peer frees its buffer via the new ack_sn
    return out;
}

AmRx::Outcome AmRx::rx(const std::vector<uint8_t>& pdu) {
    Outcome out;
    if (pdu.size() < 3) return out;
    const uint8_t b0 = pdu[0];
    if ((b0 & 0xC0) != kAmDcData) return out; // not a data PDU
    const uint8_t fi = static_cast<uint8_t>(b0 & kAmFiMask);
    const bool poll = (b0 & kAmPollBit) != 0;
    const uint16_t seq = get_u16(pdu, 1);

    if (!seen_.insert(seq).second) return out;      // duplicate PDU
    if (seen_.size() > 8192) seen_.erase(seen_.begin()); // bound the memory
    if (seq < vr_next_) return out;                 // stale retransmission

    if (seq > vr_next_) {
        // Record every skipped SN as a hole and hold this PDU for in-order
        // delivery once the gap fills.
        for (uint32_t sn = vr_next_; sn < seq && holes_.size() < kMaxHoles; ++sn) {
            holes_.insert(static_cast<uint16_t>(sn));
            ++gaps_;
            out.status_needed = true;
        }
        if (reorder_.size() < kMaxReorder) {
            reorder_[seq] = {fi,
                             std::vector<uint8_t>(pdu.begin() + 3, pdu.end())};
        } else {
            ++overflows_;
        }
    } else {
        accept_in_order(seq, fi, std::vector<uint8_t>(pdu.begin() + 3, pdu.end()),
                        out);
        // Release anything that was waiting behind this SN.
        drain_reorder(out);
    }

    if (poll) out.status_needed = true;
    return out;
}

void AmRx::reset() {
    vr_next_ = 0;
    seen_.clear();
    holes_.clear();
    reorder_.clear();
    collecting_ = false;
    partial_.clear();
    accepts_ = 0;
    tick_accepts_ = 0;
    last_progress_ms_ = -1;
}

std::vector<uint8_t> AmRx::build_status() const {
    std::vector<uint8_t> s;
    s.reserve(4 + 2 * holes_.size());
    s.push_back(kAmDcControl);
    put_u16(s, vr_next_);
    s.push_back(static_cast<uint8_t>(holes_.size()));
    for (uint16_t h : holes_) put_u16(s, h);
    return s;
}

}

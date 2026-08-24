#include "core/bs_node.h"
#include "core/pdu_trace.h"
#include "mac/mac_pdu.h"
#include "nas/nas_messages.h"
#include "pdcp/pdcp_entity.h"
#include "rlc/rlc_tm.h"
#include "rrc/rrc_messages.h"
#include <algorithm>

namespace core {

BsNode::BsNode(const BsNodeConfig& config) : config_(config) {
    rach_bs_.set_send_callback([this](mac::RachMsgType type, const std::vector<uint8_t>& pdu) {
        AirFrameType frame_type = AirFrameType::DATA;
        switch (type) {
            case mac::RachMsgType::MSG2_RAR: frame_type = AirFrameType::MSG2_RAR; break;
            case mac::RachMsgType::MSG4_CONTENTION_RESOLVE:
                frame_type = AirFrameType::MSG4_CR;
                break;
            default:
                LOG_WARN(ev::BS_RACH_TX_UNEXPECTED, {{"type", std::to_string(static_cast<int>(type))}});
                return;
        }
        send_frame(frame_type, 0, pdu);
    });

    // MSG3 carries the CCCH PDU (RRC SetupRequest) after its 3-byte header.
    rach_bs_.set_ccch_handler([this](uint16_t crnti, const std::vector<uint8_t>& ccch) {
        rrc_bs_.handle_message(crnti, ccch);
    });

    rrc_bs_.set_send_callback([this](uint16_t rnti, const std::vector<uint8_t>& pdu) {
        downlink_send(rnti, mac::LCID_CCCH, pdu);
    });

    nas_bs_.set_send_callback([this](uint32_t tmsi, const std::vector<uint8_t>& pdu) {
        uint16_t crnti = ul_crnti_;
        auto it = tmsi_to_crnti_.find(tmsi);
        if (it != tmsi_to_crnti_.end()) {
            crnti = it->second;
        } else {
            tmsi_to_crnti_[tmsi] = ul_crnti_; // fresh accept for this UE
        }
        auto pdcp_pdu = pdcp::tx(rlc::tm_tx(pdu));
        downlink_send(crnti, mac::LCID_NAS_DCCH, pdcp_pdu);
    });
}

void BsNode::set_air_send(AirBitsSend send) { air_send_ = std::move(send); }

void BsNode::tick(uint32_t now_ms) {
    now_ms_ = std::max(now_ms, now_ms_);
    timers_.tick(now_ms_);
    pump_harq();
}

void BsNode::pump_harq() {
    for (auto& e : harq_tx_.poll_timeouts(now_ms_)) {
        // Retransmissions go back to the flow's addressed UE; with a single
        // active UE today the last uplink C-RNTI identifies it.
        trace_pdu("HARQ", "TX", "retx", e.coded);
        send_frame(AirFrameType::DATA, ul_crnti_, e.coded);
    }
}

void BsNode::send_ack(uint16_t to, const HarqRx::Result& res) {
    if (!res.need_feedback || to == 0 || to == mac::RNTI_BROADCAST) return;
    if (res.proc == 0x7F) return;
    harq_tx_.advance(now_ms_);
    downlink_raw(to, mac::LCID_HARQ_ACK,
                 {static_cast<uint8_t>(res.proc & 0x7F),
                  static_cast<uint8_t>(res.ack ? 1 : 0)});
}

void BsNode::start_broadcast() {
    broadcast_sib();
    timers_.schedule(config_.sib_period_ms, true, [this] { broadcast_sib(); });
}

void BsNode::broadcast_sib() {
    auto mib_pdu = rrc_bs_.broadcast_mib().encode();
    auto sib1_pdu = rrc_bs_.broadcast_sib1().encode();
    std::vector<uint8_t> pdu =
        mac::build_pdu({{mac::LCID_MIB, mib_pdu}, {mac::LCID_SIB1, sib1_pdu}});
    // Broadcasts use the same FEC framing but a dedicated process id and no
    // feedback (nothing to combine against across multiple receivers).
    auto payload = link_encode(pdu, 0x7F, 1);
    trace_pdu("MAC", "TX", "sib broadcast", pdu);
    send_frame(AirFrameType::DATA, mac::RNTI_BROADCAST, payload);
}

void BsNode::on_air_bits(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    if (!unpack_air_bits(bits, bytes)) return;
    AirFrame frame;
    if (!decode_frame(bytes.data(), bytes.size(), frame)) {
        LOG_WARN(ev::AIR_FRAME_DECODE_FAIL, {{"len", std::to_string(bits.size())}});
        return;
    }
    handle_air_frame(frame);
}

void BsNode::handle_air_frame(const AirFrame& frame) {
    switch (frame.type) {
        case AirFrameType::MSG1_PRACH:
            if (frame.payload.size() >= 2) {
                rach_bs_.on_prach_received(frame.payload[1]);
            }
            break;
        case AirFrameType::MSG3_CCCH: {
            if (frame.payload.size() >= 4) {
                uint16_t ra_rnti = static_cast<uint16_t>(frame.payload[1] |
                                                         (frame.payload[2] << 8));
                rach_bs_.on_msg3_received(ra_rnti, frame.payload);
            }
            break;
        }
        case AirFrameType::DATA:
            handle_dl_data(frame.rnti, frame.payload);
            break;
        default: // MSG2/MSG4 are downlink-only
            break;
    }
}

void BsNode::handle_dl_data(uint16_t rnti, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> pdu;
    if (is_harq_framed(payload)) {
        auto res = harq_rx_.receive(payload);
        send_ack(rnti, res);
        if (!res.delivered) return;
        pdu = std::move(res.mac_pdu);
    } else {
        pdu = payload; // HARQ-ACK control frames arrive unframed
    }
    trace_pdu("MAC", "RX", "ul data", pdu);
    ul_crnti_ = rnti;
    for (auto& [lcid, sdu] : mac::parse_pdu(pdu)) {
        if (sdu.empty()) continue;
        switch (lcid) {
            case mac::LCID_CCCH: {
                trace_pdu("RRC", "RX", "ccch", sdu);
                rrc_bs_.handle_message(rnti, sdu);
                break;
            }
            case mac::LCID_NAS_DCCH: {
                auto nas_pdu = pdcp::rx(rlc::tm_rx(sdu));
                trace_pdu("NAS", "RX", "dcch", nas_pdu);
                auto decoded = nas::NasMessage::decode(nas_pdu);
                bool detach = decoded.msg_type == nas::NasMessageType::DETACH;
                nas_bs_.handle_message(0, nas_pdu);
                if (detach) {
                    for (auto it = tmsi_to_crnti_.begin();
                         it != tmsi_to_crnti_.end();) {
                        if (it->second == rnti) it = tmsi_to_crnti_.erase(it);
                        else ++it;
                    }
                    harq_tx_.reset();
                    harq_rx_.reset();
                }
                break;
            }
            case mac::LCID_HARQ_ACK: {
                // UE feedback for our downlink transport blocks.
                if (sdu.size() < 2) break;
                uint8_t proc = sdu[0] & 0x7F;
                harq_tx_.advance(now_ms_);
                if (sdu[1]) {
                    harq_tx_.on_ack(proc);
                } else if (auto e = harq_tx_.on_nack(proc)) {
                    trace_pdu("HARQ", "TX", "retx(nack)", e->coded);
                    send_frame(AirFrameType::DATA, rnti, e->coded);
                }
                break;
            }
            case mac::LCID_APP_DTCH: {
                auto data = pdcp::rx(rlc::tm_rx(sdu));
                trace_pdu("APP", "RX", "ping", data);
                LOG_INFO(ev::APP_ECHO_TX, {{"len", std::to_string(data.size())}});
                downlink_send(rnti, mac::LCID_APP_DTCH, sdu); // loop back as-is
                break;
            }
            default:
                break;
        }
    }
}

void BsNode::downlink_send(uint16_t rnti, uint8_t lcid,
                           const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu = mac::build_pdu({{lcid, sdu}});
    trace_pdu("MAC", "TX", "dl data", pdu);
    auto ev = harq_tx_.send(pdu);
    if (!ev.has_value()) return; // processes busy: timeout path retries later
    send_frame(AirFrameType::DATA, rnti, ev->coded);
}

void BsNode::downlink_raw(uint16_t rnti, uint8_t lcid,
                          const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu = mac::build_pdu({{lcid, sdu}});
    send_frame(AirFrameType::DATA, rnti, pdu);
}

void BsNode::send_frame(AirFrameType type, uint16_t rnti,
                        const std::vector<uint8_t>& payload) {
    if (!air_send_) return;
    AirFrame frame;
    frame.type = type;
    frame.rnti = rnti;
    frame.payload = payload;
    air_send_(pack_air_bits(encode_frame(frame)));
}

}

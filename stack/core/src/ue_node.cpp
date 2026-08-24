#include "core/ue_node.h"
#include "core/pdu_trace.h"
#include "mac/mac_pdu.h"
#include "pdcp/pdcp_entity.h"
#include "rlc/rlc_tm.h"
#include "rrc/rrc_messages.h"
#include <algorithm>

namespace core {

UeNode::UeNode(const UeNodeConfig& config) : config_(config), rng_(config.rng_seed) {
    // The node-level windows are authoritative for the orchestrated loop;
    // RachConfig only contributes preamble index and retry limits.
    mac::RachConfig rach = config_.rach;
    rach.rar_window_ms = static_cast<uint16_t>(config_.rar_window_ms);
    rach.content_resolve_window_ms = static_cast<uint16_t>(config_.cr_window_ms);
    rach_ue_ = mac::RachUe(rach);

    rach_ue_.set_msg3_provider([this] { return pending_ccch_; });

    rach_ue_.set_send_callback([this](mac::RachMsgType type, const std::vector<uint8_t>& pdu) {
        AirFrameType frame_type = AirFrameType::DATA;
        switch (type) {
            case mac::RachMsgType::MSG1_PRACH: frame_type = AirFrameType::MSG1_PRACH; break;
            case mac::RachMsgType::MSG3_RRC_REQ: frame_type = AirFrameType::MSG3_CCCH; break;
            default:
                LOG_WARN(ev::UE_RACH_TX_UNEXPECTED, {{"type", std::to_string(static_cast<int>(type))}});
                return;
        }
        send_frame(frame_type, 0, pdu);
    });

    rach_ue_.set_state_callback([this](mac::RachState /*old_s*/, mac::RachState new_s) {
        if (new_s == mac::RachState::WAIT_RAR) {
            crnti_cache_ = 0;
            schedule_rach_window_timer();
        } else if (new_s == mac::RachState::WAIT_CONTENTION_RESOLVE) {
            schedule_rach_window_timer(); // now guarding contention resolution
        } else if (new_s == mac::RachState::IDLE) {
            if (rach_window_timer_running_) {
                timers_.cancel(rach_window_timer_);
                rach_window_timer_running_ = false;
            }
            schedule_attach_retry(); // fault recovery (M7.4): try again
        }
    });

    // RRC PDUs travel on CCCH. Before a C-RNTI exists the encoded
    // SetupRequest is stashed and picked up as the MSG3 payload; afterwards
    // it goes out as dedicated uplink data.
    rrc_ue_.set_send_callback([this](const std::vector<uint8_t>& pdu) {
        if (crnti_cache_ != 0) {
            uplink_send(mac::LCID_CCCH, pdu);
        } else {
            pending_ccch_ = pdu; // MSG3 provider hands this to RachUe
        }
    });

    nas_ue_.set_send_callback([this](const std::vector<uint8_t>& pdu) {
        auto pdcp_pdu = pdcp::tx(rlc::tm_tx(pdu));
        uplink_send(mac::LCID_NAS_DCCH, pdcp_pdu);
    });
}

void UeNode::set_air_send(AirBitsSend send) { air_send_ = std::move(send); }

void UeNode::tick(uint32_t now_ms) {
    now_ms_ = std::max(now_ms, now_ms_);
    timers_.tick(now_ms_);
}

void UeNode::attach() {
    attach_requested_ = true;
    maybe_start_attach();
}

void UeNode::maybe_start_attach() {
    if (!attach_requested_) return;
    if (!has_system_info()) {
        LOG_INFO(ev::UE_ATTACH_PENDING_SIB, {});
        return;
    }
    if (rach_ue_.state() != mac::RachState::IDLE ||
        rrc_ue_.state() != rrc::UeState::IDLE) {
        return; // procedure already in flight or connected
    }

    LOG_INFO(ev::UE_ATTACH_START, {{"imsi", config_.imsi}});
    pending_ccch_.clear();
    rrc_ue_.start_connection(); // fills pending_ccch_ via send callback

    if (!attach_guard_running_) {
        attach_guard_running_ = true;
        timers_.schedule(config_.attach_guard_ms, false, [this] {
            attach_guard_running_ = false;
            if (!registered()) {
                abort_attach("guard_timeout");
            }
        });
    }

    rach_ue_.start_rach();
}

void UeNode::abort_attach(const char* reason) {
    LOG_ERROR(ev::ATTACH_ABORT, {{"reason", reason}});
    attach_requested_ = false;
    attach_guard_running_ = false;
    pending_ccch_.clear();
    rach_ue_.force_idle();
    rrc_ue_.force_idle();
    crnti_cache_ = 0;
    nas_ue_.force_deregistered();
}

void UeNode::detach() {
    if (!registered()) {
        LOG_WARN(ev::UE_DETACH_IGNORED, {{"state", "not registered"}});
        return;
    }
    nas_ue_.send_detach();  // UL DCCH while C-RNTI is still cached
    rrc_ue_.release();      // UL CCCH
    crnti_cache_ = 0;
    rach_ue_.force_idle();
    pending_ccch_.clear();
    attach_requested_ = false;
    LOG_INFO(ev::UE_DETACH_DONE, {});
}

void UeNode::send_app_data(const std::vector<uint8_t>& payload) {
    if (!registered()) {
        LOG_WARN(ev::APP_TX_NO_CONTEXT, {});
        return;
    }
    ++app_tx_seq_;
    uint32_t seq = app_tx_seq_;
    app_tx_time_[seq] = now_ms_;

    std::vector<uint8_t> framed;
    framed.push_back(static_cast<uint8_t>(seq & 0xFF));
    framed.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));
    framed.push_back(static_cast<uint8_t>((seq >> 16) & 0xFF));
    framed.push_back(static_cast<uint8_t>((seq >> 24) & 0xFF));
    framed.insert(framed.end(), payload.begin(), payload.end());

    trace_pdu("APP", "TX", "ping", framed);
    auto pdcp_pdu = pdcp::tx(rlc::tm_tx(framed));
    uplink_send(mac::LCID_APP_DTCH, pdcp_pdu);
}

void UeNode::on_air_bits(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    if (!unpack_air_bits(bits, bytes)) return;
    AirFrame frame;
    if (!decode_frame(bytes.data(), bytes.size(), frame)) {
        LOG_WARN(ev::AIR_FRAME_DECODE_FAIL, {{"len", std::to_string(bits.size())}});
        return;
    }
    handle_air_frame(frame);
}

void UeNode::handle_air_frame(const AirFrame& frame) {
    switch (frame.type) {
        case AirFrameType::MSG2_RAR:
        case AirFrameType::MSG4_CR:
            handle_rach_payload(frame.type, frame.rnti, frame.payload);
            break;
        case AirFrameType::DATA:
            handle_data_pdu(frame.rnti, frame.payload);
            break;
        default: // MSG1/MSG3 are uplink-only; ignore on the UE side
            break;
    }
}

void UeNode::handle_rach_payload(AirFrameType type, uint16_t /*rnti*/,
                                 const std::vector<uint8_t>& payload) {
    // RachUe/RachBs PDU layout keeps its own 1-byte RachMsgType header:
    //   MSG2: [type][ra_rnti LE][timing_advance][ul_grant]
    //   MSG4: [type][c_rnti LE]
    if (type == AirFrameType::MSG2_RAR && payload.size() >= 5) {
        uint16_t ra_rnti = static_cast<uint16_t>(payload[1] | (payload[2] << 8));
        rach_ue_.on_rar_received(ra_rnti, payload[3], payload[4]);
    } else if (type == AirFrameType::MSG4_CR && payload.size() >= 3) {
        uint16_t crnti = static_cast<uint16_t>(payload[1] | (payload[2] << 8));
        rach_ue_.on_contention_resolve(crnti);
        crnti_cache_ = crnti;
    }
}

void UeNode::handle_data_pdu(uint16_t rnti, const std::vector<uint8_t>& pdu) {
    trace_pdu("MAC", "RX", rnti == mac::RNTI_BROADCAST ? "broadcast" : "dl data", pdu);
    for (auto& [lcid, sdu] : mac::parse_pdu(pdu)) {
        if (lcid == mac::LCID_MIB || lcid == mac::LCID_SIB1) {
            handle_sysinfo_sdu(lcid, sdu);
        } else if (rnti != mac::RNTI_BROADCAST && !sdu.empty()) {
            handle_dedicated_sdu(lcid, sdu);
        }
    }
}

void UeNode::handle_sysinfo_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu) {
    if (lcid == mac::LCID_MIB && sdu.size() >= 5) {
        auto mib = rrc::Mib::decode(sdu);
        mib_ok_ = true;
        rrc_ue_.on_mib_received(mib);
    } else if (lcid == mac::LCID_SIB1 && sdu.size() >= 7) {
        auto sib1 = rrc::Sib1::decode(sdu);
        sib1_ok_ = true;
        rrc_ue_.on_sib1_received(sib1);
    } else {
        LOG_WARN(ev::SYSINFO_DECODE_FAIL, {{"lcid", std::to_string(lcid)}});
        return;
    }
    maybe_start_attach(); // SIB-gated attach may now proceed
}

void UeNode::handle_dedicated_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu) {
    switch (lcid) {
        case mac::LCID_CCCH: {
            trace_pdu("RRC", "RX", "dcch", sdu);
            rrc_ue_.on_message(sdu);
            // Connection is up: proceed with the NAS attach if one is pending.
            if (attach_requested_ && nas_ue_.state() == nas::UeState::DEREGISTERED &&
                rrc_ue_.state() == rrc::UeState::CONNECTED) {
                nas_ue_.send_attach_request(config_.imsi);
            }
            break;
        }
        case mac::LCID_NAS_DCCH: {
            auto nas_pdu = pdcp::rx(rlc::tm_rx(sdu));
            trace_pdu("NAS", "RX", "dcch", nas_pdu);
            nas_ue_.on_message(nas_pdu);
            break;
        }
        case mac::LCID_APP_DTCH: {
            auto data = pdcp::rx(rlc::tm_rx(sdu));
            trace_pdu("APP", "RX", "pong", data);
            if (data.size() >= 4) {
                uint32_t seq = static_cast<uint32_t>(data[0]) |
                               (static_cast<uint32_t>(data[1]) << 8) |
                               (static_cast<uint32_t>(data[2]) << 16) |
                               (static_cast<uint32_t>(data[3]) << 24);
                ++app_rx_count_;
                auto it = app_tx_time_.find(seq);
                if (it != app_tx_time_.end()) {
                    last_app_rtt_ms_ = static_cast<int64_t>(now_ms_) - it->second;
                    LOG_INFO(ev::APP_RTT, {{"seq", std::to_string(seq)},
                                          {"rtt_ms", std::to_string(last_app_rtt_ms_)}});
                    app_tx_time_.erase(it);
                }
            }
            break;
        }
        default:
            break;
    }
}

void UeNode::uplink_send(uint8_t lcid, const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu = mac::build_pdu({{lcid, sdu}});
    trace_pdu("MAC", "TX", "ul data", pdu);
    send_frame(AirFrameType::DATA, crnti_cache_, pdu);
}

void UeNode::send_frame(AirFrameType type, uint16_t rnti,
                        const std::vector<uint8_t>& payload) {
    if (!air_send_) return;
    AirFrame frame;
    frame.type = type;
    frame.rnti = rnti;
    frame.payload = payload;
    air_send_(pack_air_bits(encode_frame(frame)));
}

void UeNode::schedule_attach_retry() {
    // A RACH attempt collapsed (RAR/CR timeout, retry budget exhausted).
    // While the user still wants attach and the guard timer has not fired,
    // reset the upper states and re-run the procedure after a short jitter.
    if (!attach_requested_ || registered()) return;

    std::uniform_int_distribution<uint32_t> dist(50, 200);
    uint32_t delay = dist(rng_);
    LOG_INFO(ev::ATTACH_RETRY, {{"delay_ms", std::to_string(delay)}});
    timers_.schedule(delay, false, [this] {
        if (!attach_requested_ || registered()) return;
        if (rach_ue_.state() != mac::RachState::IDLE) return;
        if (rrc_ue_.state() != rrc::UeState::IDLE) {
            rrc_ue_.force_idle(); // silent local reset, pending_ccch_ is rebuilt
        }
        crnti_cache_ = 0;
        maybe_start_attach();
    });
}

void UeNode::schedule_rach_window_timer() {
    if (rach_window_timer_running_) {
        timers_.cancel(rach_window_timer_);
    }
    rach_window_timer_running_ = true;
    bool waiting_cr = rach_ue_.state() == mac::RachState::WAIT_CONTENTION_RESOLVE;
    uint32_t window = waiting_cr ? config_.cr_window_ms : config_.rar_window_ms;
    rach_window_timer_ = timers_.schedule(window, false, [this] {
        rach_window_timer_running_ = false;
        if (rach_ue_.state() == mac::RachState::WAIT_RAR) {
            schedule_backoff_then_retry();
        } else if (rach_ue_.state() == mac::RachState::WAIT_CONTENTION_RESOLVE) {
            rach_ue_.on_contention_resolve_timeout();
        }
    });
}

void UeNode::schedule_backoff_then_retry() {
    std::uniform_int_distribution<uint32_t> dist(config_.backoff_min_ms,
                                                 config_.backoff_max_ms);
    uint32_t delay = dist(rng_);
    LOG_WARN(ev::RACH_BACKOFF, {{"delay_ms", std::to_string(delay)}});
    timers_.schedule(delay, false, [this] {
        if (rach_ue_.state() == mac::RachState::WAIT_RAR) {
            rach_ue_.on_rar_timeout(); // resends MSG1 or gives up -> IDLE
        }
    });
}

}

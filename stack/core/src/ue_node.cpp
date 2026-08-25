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
    pump_harq();
}

void UeNode::pump_harq() {
    for (auto& e : harq_tx_.poll_timeouts(now_ms_)) {
        trace_pdu("HARQ", "TX", "retx", e.coded);
        send_frame(AirFrameType::DATA, crnti_cache_, e.coded);
    }
}

void UeNode::send_ack(uint16_t to, const HarqRx::Result& res) {
    if (!res.need_feedback || to == 0 || to == mac::RNTI_BROADCAST) return;
    if (res.proc == 0x7F) return; // broadcast blocks carry no feedback
    harq_tx_.advance(now_ms_);
    std::vector<uint8_t> pdu = mac::build_pdu(
        {{mac::LCID_HARQ_ACK,
          {static_cast<uint8_t>(res.proc & 0x7F),
           static_cast<uint8_t>(res.ack ? 1 : 0)}}});
    trace_pdu("HARQ", "TX", res.ack ? "ack" : "nack", pdu);
    send_frame(AirFrameType::DATA, to, pdu);
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
    stop_traffic();
    harq_tx_.reset();
    harq_rx_.reset();
    up_sec_on_ = false;
    pdcp_seq_ = 0;
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
    stop_traffic();
    harq_tx_.reset();
    harq_rx_.reset();
    up_sec_on_ = false;
    pdcp_seq_ = 0;
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

void UeNode::start_traffic(uint32_t interval_ms) {
    if (!registered()) {
        LOG_WARN(ev::APP_TX_NO_CONTEXT, {});
        return;
    }
    if (traffic_timer_ != 0) return; // already running

    traffic_timer_ = timers_.schedule(interval_ms, true, [this, interval_ms] {
        std::vector<uint8_t> payload(32);
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<uint8_t>('A' + (i % 26));
        }
        send_app_data(payload);
    });

    if (loss_sweep_timer_ == 0) {
        loss_sweep_timer_ = timers_.schedule(500, true, [this] { sweep_lost_pings(); });
    }
    if (stats_timer_ == 0) {
        stats_timer_ = timers_.schedule(5000, true, [this] { emit_traffic_stats(); });
    }
    LOG_INFO(ev::TRAFFIC_START, {{"interval_ms", std::to_string(interval_ms)}});
}

void UeNode::stop_traffic() {
    bool was_running = traffic_timer_ != 0;
    for (TimerId* t : {&traffic_timer_, &loss_sweep_timer_, &stats_timer_}) {
        if (*t != 0) {
            timers_.cancel(*t);
            *t = 0;
        }
    }
    if (was_running) emit_traffic_stats();
    if (was_running) LOG_INFO(ev::TRAFFIC_STOP, {});
}

void UeNode::sweep_lost_pings() {
    // TM bearer: a ping unanswered well beyond any legitimate RTT is lost.
    constexpr uint32_t kLossWindowMs = 3000;
    for (auto it = app_tx_time_.begin(); it != app_tx_time_.end();) {
        if (static_cast<int32_t>(now_ms_ - it->second) >=
            static_cast<int32_t>(kLossWindowMs)) {
            LOG_WARN(ev::APP_LOSS, {{"seq", std::to_string(it->first)}});
            it = app_tx_time_.erase(it);
            ++app_loss_count_;
        } else {
            ++it;
        }
    }
}

void UeNode::emit_traffic_stats() {
    LOG_INFO(ev::TRAFFIC_STATS,
             {{"tx", std::to_string(app_tx_seq_)},
              {"rx", std::to_string(app_rx_count_)},
              {"loss", std::to_string(app_loss_count_)},
              {"rtt_min", std::to_string(rtt_min_ms_ == INT64_MAX ? -1 : rtt_min_ms_)},
              {"rtt_max", std::to_string(rtt_max_ms_)},
              {"rtt_avg", std::to_string(rtt_avg_ms())}});
}

void UeNode::on_air_bits(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    if (!unpack_air_bits(bits, bytes)) {
        return;
    }
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
            // Shared medium: only consume RAR while we are waiting for one.
            if (rach_ue_.state() == mac::RachState::WAIT_RAR) {
                handle_rach_payload(frame.type, frame.rnti, frame.payload);
            }
            break;
        case AirFrameType::MSG4_CR:
            // Only a UE in contention resolution may claim the C-RNTI.
            if (rach_ue_.state() == mac::RachState::WAIT_CONTENTION_RESOLVE) {
                handle_rach_payload(frame.type, frame.rnti, frame.payload);
            }
            break;
        case AirFrameType::DATA:
            // Shared medium: ignore unicast bursts addressed to other UEs.
            if (frame.rnti != crnti_cache_ &&
                frame.rnti != mac::RNTI_BROADCAST) {
                break;
            }
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

void UeNode::handle_data_pdu(uint16_t rnti, const std::vector<uint8_t>& payload) {
    // DATA frames come in two flavours (M9): HARQ transport blocks (user
    // traffic) and legacy raw MAC PDUs (HARQ-ACK control). The magic byte
    // in the HARQ header tells them apart.
    std::vector<uint8_t> pdu;
    if (is_harq_framed(payload)) {
        auto res = harq_rx_.receive(payload);
        send_ack(rnti, res);
        if (!res.delivered) return;
        pdu = std::move(res.mac_pdu);
    } else {
        pdu = payload; // control frame: no HARQ feedback
    }
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
            harq_rx_.reset(); // fresh connection: no cross-attach soft memory
            // Connection is up: proceed with the NAS attach if one is pending.
            if (attach_requested_ && nas_ue_.state() == nas::UeState::DEREGISTERED &&
                rrc_ue_.state() == rrc::UeState::CONNECTED) {
                nas_ue_.send_attach_request(config_.imsi);
            }
            break;
        }
        case mac::LCID_NAS_DCCH: {
            // The protected frame carries the same legacy-wrapped PDCP
            // payload as the clear path; decrypt first, then unwrap.
            std::vector<uint8_t> nas_pdu;
            if (up_sec_on_) {
                std::vector<uint8_t> inner;
                if (!pdcp::unprotect(up_key_, sdu, inner)) {
                    LOG_WARN(ev::SEC_DECRYPT_FAIL, {{"layer", "NAS"}});
                    break;
                }
                nas_pdu = pdcp::rx(rlc::tm_rx(inner));
            } else {
                nas_pdu = pdcp::rx(rlc::tm_rx(sdu));
            }
            trace_pdu("NAS", "RX", "dcch", nas_pdu);
            nas_ue_.on_message(nas_pdu);
            if (nas_ue_.state() == nas::UeState::REGISTERED &&
                nas_ue_.authenticated() && !up_sec_on_) {
                up_key_ = nas_ue_.session_key();
                up_sec_on_ = true;
                LOG_INFO(ev::SEC_ENABLED, {{"dir", "ul"}});
            }
            break;
        }
        case mac::LCID_APP_DTCH: {
            std::vector<uint8_t> data;
            if (up_sec_on_) {
                std::vector<uint8_t> inner;
                if (!pdcp::unprotect(up_key_, sdu, inner)) {
                    LOG_WARN(ev::SEC_DECRYPT_FAIL, {{"layer", "APP"}});
                    break;
                }
                data = pdcp::rx(rlc::tm_rx(inner));
            } else {
                data = pdcp::rx(rlc::tm_rx(sdu));
            }
            trace_pdu("APP", "RX", "pong", data);
            if (data.size() >= 4) {
                uint32_t seq = static_cast<uint32_t>(data[0]) |
                               (static_cast<uint32_t>(data[1]) << 8) |
                               (static_cast<uint32_t>(data[2]) << 16) |
                               (static_cast<uint32_t>(data[3]) << 24);
                auto it = app_tx_time_.find(seq);
                if (it != app_tx_time_.end()) ++app_rx_count_; // dedupe retx
                auto it2 = app_tx_time_.find(seq);
                if (it != app_tx_time_.end()) {
                    last_app_rtt_ms_ = static_cast<int64_t>(now_ms_) - it->second;
                    app_tx_time_.erase(it);
                    ++rtt_samples_;
                    rtt_sum_ms_ += last_app_rtt_ms_;
                    rtt_min_ms_ = std::min(rtt_min_ms_, last_app_rtt_ms_);
                    rtt_max_ms_ = std::max(rtt_max_ms_, last_app_rtt_ms_);
                    LOG_INFO(ev::APP_RTT, {{"seq", std::to_string(seq)},
                                          {"rtt_ms", std::to_string(last_app_rtt_ms_)}});
                }
            }
            break;
        }
        case mac::LCID_HARQ_ACK: {
            if (sdu.size() < 2) break;
            uint8_t proc = sdu[0] & 0x7F;
            harq_tx_.advance(now_ms_);
            if (sdu[1]) {
                harq_tx_.on_ack(proc);
            } else if (auto e = harq_tx_.on_nack(proc)) {
                trace_pdu("HARQ", "TX", "retx(nack)", e->coded);
                send_frame(AirFrameType::DATA, crnti_cache_, e->coded);
            }
            break;
        }
        default:
            break;
    }
}

void UeNode::uplink_send(uint8_t lcid, const std::vector<uint8_t>& sdu_in) {
    std::vector<uint8_t> sdu = sdu_in;
    const bool ciphered =
        up_sec_on_ && (lcid == mac::LCID_NAS_DCCH || lcid == mac::LCID_APP_DTCH);
    if (ciphered) {
        sdu = pdcp::protect(up_key_, pdcp_seq_, sdu);
        ++pdcp_seq_;
    }
    std::vector<uint8_t> pdu = mac::build_pdu({{lcid, sdu}});
    trace_pdu("MAC", "TX", ciphered ? "ul enc" : "ul data", pdu);

    auto ev = harq_tx_.send(pdu);
    if (!ev.has_value()) {
        LOG_WARN(ev::HARQ_DROP, {{"proc", "255"},
                                  {"attempts", "0"},
                                  {"reason", "tx_busy"}});
        return; // all processes busy: upper-layer loss accounting applies
    }
    send_frame(AirFrameType::DATA, crnti_cache_, ev->coded);
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

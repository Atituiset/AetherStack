#include "core/ue_node.h"
#include "core/pdu_trace.h"
#include "mac/mac_pdu.h"
#include "pdcp/pdcp_entity.h"
#include "rlc/rlc_tm.h"
#include "rrc/rrc_messages.h"
#include <algorithm>
#include <cmath>

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

    // M20: the network suspended us — park the data path (context, keys,
    // bearers, dialogs and NAS registration all stay).
    rrc_ue_.set_suspend_callback([this](uint32_t /*resume_id*/) {
        harq_tx_.reset();
        harq_rx_.reset();
        suspended_crnti_ = crnti_cache_;
        crnti_cache_ = 0;
    });
    // M20: resume answered — restore the data path, or fall back to a full
    // setup when the network no longer knows our resume identity.
    rrc_ue_.set_resume_callback([this](bool ok, uint16_t new_crnti) {
        resume_guard_running_ = false;
        if (ok) {
            crnti_cache_ = new_crnti;
            harq_tx_.reset();
            harq_rx_.reset();
            pump_app_bearers(); // flush whatever queued while inactive
            if (detach_after_resume_) {
                detach_after_resume_ = false;
                detach();
            }
        } else {
            // Stale resume_id: the BS context is gone — re-attach fully.
            suspended_crnti_ = 0;
            crnti_cache_ = 0;
            rach_ue_.force_idle(); // resume RACH left it CONNECTED
            nas_ue_.force_deregistered();
            stop_streams(false);
            attach_requested_ = true;
            maybe_start_attach();
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
    // M19: link-metrics service (CQI CE, open-loop power, telemetry).
    if (registered() && link_timer_ == 0) {
        link_timer_ = timers_.schedule(1000, true,
                                       [this] { service_link_metrics(); });
    }
    // M13/M17: per-bearer liveness. AM bearers (sig, best-effort):
    // probe-retransmit + reorder resync. UM bearers (voice, video):
    // reorder timer only — no ARQ, losses are accepted.
    for (Qci qci : {Qci::SIG, Qci::BEST_EFFORT}) {
        auto& b = bearers_.am_of(qci);
        for (const auto& pdu : b.tx.tick(now_ms_)) {
            enqueue_app_pdu(qci, lcid_of(qci), pdcp::tx(rlc::tm_tx(pdu)));
        }
        service_am_rx(qci);
    }
    for (Qci qci : {Qci::VOICE, Qci::VIDEO}) {
        service_um_rx(qci);
    }
    pump_app_bearers();
    // M14: radio link failure watchdog while connected.
    if (config_.radio_link_failure_ms != 0 && !reestablish_pending_ &&
        rrc_ue_.state() == rrc::UeState::CONNECTED && last_dl_ms_ != 0 &&
        now_ms_ - last_dl_ms_ > config_.radio_link_failure_ms) {
        declare_rlf();
    }
}

// M17: drain the QoS bearer queues into the HARQ pipe, strict priority
// (ctrl > sig > voice > video > best-effort with a BE min-share guard).
void UeNode::pump_app_bearers() {
    if (rrc_ue_.inactive()) return; // M20: UL parked until resume completes
    while (!bearers_.empty() &&
           harq_tx_.in_flight() < harq_tx_.num_processes()) {
        AppPdu pdu = bearers_.pop_next();
        uplink_send(pdu.lcid, pdu.bytes);
    }
}

void UeNode::enqueue_app_pdu(Qci qci, uint8_t lcid,
                             const std::vector<uint8_t>& pdu) {
    auto& q = bearers_.queue_of(qci);
    // Media backpressure: a congested pipe drops stale media instead of
    // queueing it (the receiver counts the sequence gap as loss). AM
    // bearers are bounded by their own window and also capped here.
    constexpr size_t kBearerQueueLimit = 64;
    if (q.size() >= kBearerQueueLimit) {
        q.pop_front();
    }
    q.push_back({lcid, false, pdu});
}

// M16.1: AM reordering timeout on one downlink bearer — resync (and deliver
// whatever the skip freed) instead of wedging on a lost SN.
void UeNode::service_am_rx(Qci qci) {
    auto& b = bearers_.am_of(qci);
    auto am_tick = b.rx.tick(now_ms_);
    for (const auto& data : am_tick.delivered) {
        handle_app_sdu(data);
    }
    if (am_tick.status_needed) {
        auto status = b.rx.build_status();
        LOG_INFO(ev::RLC_AM_STATUS_TX,
                 {{"dir", "ul"}, {"nacks", std::to_string(status[2])}});
        enqueue_app_pdu(qci, status_lcid_of(qci), status);
    }
}

// M17: UM bearer receive service — drive the reorder timer and hand
// delivered SDUs to the app dispatch. No STATUS, no retransmission.
void UeNode::service_um_rx(Qci qci) {
    auto& b = bearers_.um_of(qci);
    b.rx.tick(now_ms_);
    for (const auto& data : b.rx.poll()) {
        handle_app_sdu(data);
    }
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

    if (reestablish_pending_) {
        // M14: a re-establishment is in progress (its guard timer runs);
        // retries must keep carrying the re-establishment request, not a
        // fresh SetupRequest.
        pending_ccch_ = reest_req_pdu_;
        LOG_INFO(ev::ATTACH_RETRY, {{"delay_ms", "0"}});
        rach_ue_.start_rach();
        return;
    }

    // M22: with several cells audible, camp on the strongest and target
    // the RACH at it (preamble partitioning).
    select_serving_cell();
    rach_ue_.set_preamble_index(
        mac::preamble_for_cell(config_.rach.preamble_index, serving_cell_));
    LOG_INFO(ev::UE_ATTACH_START, {{"imsi", config_.imsi}});
    pending_ccch_.clear();
    rrc_ue_.start_connection(serving_cell_); // fills pending_ccch_ via cb

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
    stop_streams(false); // link is gone: drop streams without notifying
    stop_measurements();
    harq_tx_.reset();
    harq_rx_.reset();
    for (Qci qci : {Qci::SIG, Qci::BEST_EFFORT}) {
        auto& b = bearers_.am_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
        b.established = false;
    }
    for (Qci qci : {Qci::VOICE, Qci::VIDEO}) {
        auto& b = bearers_.um_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
        b.established = false;
    }
    up_sec_on_ = false;
    pdcp_seq_ = 0;
}

void UeNode::detach() {
    if (!registered()) {
        LOG_WARN(ev::UE_DETACH_IGNORED, {{"state", "not registered"}});
        return;
    }
    if (rrc_ue_.inactive()) {
        // M20: resume first (fast), then a normal connected detach so the
        // network context is torn down cleanly.
        detach_after_resume_ = true;
        wake();
        return;
    }
    stop_streams(true);   // hang up first, while the uplink still works
    nas_ue_.send_detach();  // UL DCCH while C-RNTI is still cached
    rrc_ue_.release();      // UL CCCH
    crnti_cache_ = 0;
    rach_ue_.force_idle();
    pending_ccch_.clear();
    attach_requested_ = false;
    stop_traffic();
    stop_measurements();
    harq_tx_.reset();
    harq_rx_.reset();
    for (Qci qci : {Qci::SIG, Qci::BEST_EFFORT}) {
        auto& b = bearers_.am_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
        b.established = false;
    }
    for (Qci qci : {Qci::VOICE, Qci::VIDEO}) {
        auto& b = bearers_.um_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
        b.established = false;
    }
    up_sec_on_ = false;
    pdcp_seq_ = 0;
    LOG_INFO(ev::UE_DETACH_DONE, {});
}

// M20: ask the network to suspend us to RRC_INACTIVE (the BS answers with
// a suspend RELEASE carrying the resume identity).
void UeNode::sleep() {
    if (!registered() || rrc_ue_.state() != rrc::UeState::CONNECTED) return;
    rrc_ue_.request_suspend();
}

// M20: resume from RRC_INACTIVE — RACH + RRCResumeRequest{resume_id}. The
// kept context (security, bearers, registration) is simply re-adopted, so
// this is much cheaper than a full attach; see docs/m20_plan.md.
void UeNode::wake() {
    if (!rrc_ue_.inactive()) return;
    const uint32_t id = rrc_ue_.resume_id();
    rrc::RrcMessage req;
    req.msg_type = rrc::RrcMessageType::RESUME_REQUEST;
    for (int i = 0; i < 4; ++i) {
        req.value.push_back(static_cast<uint8_t>((id >> (8 * i)) & 0xFF));
    }
    pending_ccch_ = req.encode(); // MSG3 payload, like a SetupRequest
    rrc_ue_.start_resume();       // INACTIVE -> CONNECTING
    LOG_INFO(ev::RRC_RESUME_REQUEST, {{"resume_id", std::to_string(id)}});
    rach_ue_.force_idle(); // still CONNECTED from the original attach RACH
    // M22: target the resume RACH at the cell holding our context.
    rach_ue_.set_preamble_index(
        mac::preamble_for_cell(config_.rach.preamble_index, serving_cell_));
    if (!resume_guard_running_) {
        resume_guard_running_ = true;
        // Longer than the attach guard: the RESUME_OK must survive HARQ
        // retransmission bursts on a lossy link (3 s proved too tight at
        // 5% loss — the UE fell back to full attach unnecessarily).
        timers_.schedule(4 * config_.attach_guard_ms, false, [this] {
            if (!resume_guard_running_) return;
            resume_guard_running_ = false;
            if (rrc_ue_.state() == rrc::UeState::CONNECTED) return;
            // Resume stalled — recover with a full attach.
            rach_ue_.force_idle();
            rrc_ue_.force_idle();
            suspended_crnti_ = 0;
            nas_ue_.force_deregistered();
            stop_streams(false);
            attach_requested_ = true;
            maybe_start_attach();
        });
    }
    rach_ue_.start_rach();
}

// M20: outbound activity while INACTIVE kicks off the resume; the SDUs
// queue on their bearers and flush when the data path is restored.
void UeNode::maybe_wake_for_activity() {
    if (rrc_ue_.inactive()) wake();
}

// M22: camp on the strongest audible cell (SIB reception count proxy).
void UeNode::select_serving_cell() {
    uint16_t best = 0;
    uint32_t best_rx = 0;
    for (const auto& [cell_id, c] : cells_) {
        if (c.rx_count > best_rx) {
            best_rx = c.rx_count;
            best = cell_id;
        }
    }
    if (best != 0) serving_cell_ = best;
    if (serving_cell_ == 0) serving_cell_ = 1; // nothing heard yet: cell 1
}

void UeNode::send_app_data(const std::vector<uint8_t>& payload) {
    if (!registered()) {
        LOG_WARN(ev::APP_TX_NO_CONTEXT, {});
        return;
    }
    maybe_wake_for_activity();
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
    // M13/M17: the legacy loopback rides the default (QCI9) bearer.
    auto& be = bearers_.am_of(Qci::BEST_EFFORT);
    for (const auto& am_pdu : be.tx.tx(now_ms_, framed)) {
        enqueue_app_pdu(Qci::BEST_EFFORT, mac::LCID_APP_DTCH,
                        pdcp::tx(rlc::tm_tx(am_pdu)));
    }
}

void UeNode::start_traffic(uint32_t interval_ms) {
    if (!registered()) {
        LOG_WARN(ev::APP_TX_NO_CONTEXT, {});
        return;
    }
    maybe_wake_for_activity();
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

void UeNode::on_air_bits_with_metrics(const std::vector<uint8_t>& bits,
                                      float snr_db, float pwr_dbm) {
    // EWMA over decoded DL bursts (alpha 0.3): every successfully decoded
    // burst — SIB broadcasts included — is a serving-cell measurement.
    constexpr float kAlpha = 0.3f;
    if (dl_snr_ewma_ < -90.f) {
        dl_snr_ewma_ = snr_db;
        rsrp_ewma_ = pwr_dbm;
    } else {
        dl_snr_ewma_ += kAlpha * (snr_db - dl_snr_ewma_);
        rsrp_ewma_ += kAlpha * (pwr_dbm - rsrp_ewma_);
    }
    on_air_bits(bits);
}

int UeNode::current_cqi() const {
    if (dl_snr_ewma_ < -90.f) return -1; // no measurements yet
    // Pragmatic SNR->CQI: 2 dB per index. Lines up with the MCS ladder
    // (16qam >= cqi 14, i.e. SNR ~26 dB+ — from the measured decode curve;
    // 64QAM is implemented but never selected, see docs/m19_plan.md).
    return std::clamp(1 + static_cast<int>(dl_snr_ewma_ / 2), 1, 15);
}

double UeNode::tx_gain() const {
    return std::pow(10.0, tx_power_db_ / 20.0);
}

void UeNode::service_link_metrics() {
    // --- CQI report (change-only uplink CE) -------------------------------
    // M20: no reporting while INACTIVE (the CE would just queue anyway).
    const int cqi = current_cqi();
    if (cqi > 0 && cqi != last_cqi_ &&
        rrc_ue_.state() == rrc::UeState::CONNECTED) {
        last_cqi_ = cqi;
        bearers_.ctrl().push_back(
            {mac::LCID_CQI_REPORT, false,
             {static_cast<uint8_t>(cqi)}});
    }
    // --- TX power: open loop from pathloss + closed-loop TPC accumulator --
    // Synthetic unit budget (see docs/m19_plan.md): BS transmits at a
    // reference 30 dBm, the BS wants UL arrivals at -50 dBm, so the
    // open-loop estimate is -50 + pathloss; TPC trims on top.
    if (rsrp_ewma_ > -100.f) {
        constexpr double kDlRefDbm = 30.0;
        constexpr double kUlTargetDbm = -50.0;
        const double pathloss = kDlRefDbm - rsrp_ewma_;
        const double target = std::clamp(kUlTargetDbm + pathloss +
                                             tpc_accum_db_,
                                         -30.0, 23.0);
        if (std::abs(target - tx_power_db_) >= 0.5 &&
            now_ms_ - last_pwr_log_ms_ >= 1000) {
            last_pwr_log_ms_ = now_ms_;
            tx_power_db_ = target;
            LOG_INFO(ev::TX_POWER_CHANGE,
                     {{"c_rnti", std::to_string(crnti_cache_)},
                      {"dbm", std::to_string(static_cast<int>(tx_power_db_))}});
        } else {
            tx_power_db_ = target;
        }
    }
    // --- telemetry for the LMT signal bars (~1/s) --------------------------
    if (dl_snr_ewma_ > -90.f) {
        LOG_INFO(ev::LINK_QUALITY,
                 {{"c_rnti", std::to_string(crnti_cache_)},
                  {"rsrp", std::to_string(static_cast<int>(rsrp_ewma_))},
                  {"sinr", std::to_string(static_cast<int>(dl_snr_ewma_))}});
    }
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
    //   MSG4: [type][c_rnti LE][ra_rnti LE]
    if (type == AirFrameType::MSG2_RAR && payload.size() >= 5) {
        uint16_t ra_rnti = static_cast<uint16_t>(payload[1] | (payload[2] << 8));
        rach_ue_.on_rar_received(ra_rnti, payload[3], payload[4]);
    } else if (type == AirFrameType::MSG4_CR && payload.size() >= 5) {
        uint16_t crnti = static_cast<uint16_t>(payload[1] | (payload[2] << 8));
        uint16_t ra_rnti = static_cast<uint16_t>(payload[3] | (payload[4] << 8));
        rach_ue_.on_contention_resolve(crnti, ra_rnti);
        if (rach_ue_.state() == mac::RachState::CONNECTED) crnti_cache_ = crnti;
    }
}

void UeNode::handle_data_pdu(uint16_t rnti, const std::vector<uint8_t>& payload) {
    // M14/M15: only DEDICATED downlink proves the serving link is alive.
    // SIB broadcasts keep arriving even when our connection context is gone
    // (e.g. gNB restarted), so counting them would mask a real RLF.
    if (rnti != mac::RNTI_BROADCAST) {
        last_dl_ms_ = now_ms_;
    }
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
        if (lcid == mac::LCID_MIB || lcid == mac::LCID_SIB1 ||
            lcid == mac::LCID_PAGING) {
            handle_sysinfo_sdu(lcid, sdu, rnti == mac::RNTI_BROADCAST);
        } else if (rnti != mac::RNTI_BROADCAST && !sdu.empty()) {
            handle_dedicated_sdu(lcid, sdu);
        }
    }
}

void UeNode::handle_sysinfo_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu,
                                bool broadcast) {
    if (lcid == mac::LCID_MIB && sdu.size() >= 5) {
        auto mib = rrc::Mib::decode(sdu);
        mib_ok_ = true;
        rrc_ue_.on_mib_received(mib);
    } else if (lcid == mac::LCID_SIB1 && sdu.size() >= 7) {
        auto sib1 = rrc::Sib1::decode(sdu);
        sib1_ok_ = true;
        rrc_ue_.on_sib1_received(sib1);
        // M14: neighbour-cell tracking. The first cell heard becomes the
        // serving cell; later cells accumulate as handover candidates.
        auto& c = cells_[sib1.cell_id];
        ++c.rx_count;
        c.last_seen_ms = now_ms_;
        if (serving_cell_ == 0) serving_cell_ = sib1.cell_id;
    } else if (lcid == mac::LCID_PAGING && broadcast) {
        // M14: paging record carries the IMSI in the clear (pre-M15 design).
        std::string paged_imsi(sdu.begin(), sdu.end());
        if (paged_imsi != config_.imsi) return;
        LOG_INFO(ev::PAGE_RX, {{"imsi", paged_imsi}});
        // M20: an INACTIVE UE wakes with a fast resume; an IDLE one
        // performs a full service request (fresh attach).
        if (rrc_ue_.inactive()) {
            wake();
        } else if (!registered()) {
            attach();
        }
        return;
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
            handle_ccch_sdu(lcid, sdu);
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
            if (nas_ue_.state() == nas::UeState::REGISTERED) {
                start_measurements(); // M14: connected-mode measurements
            }
            break;
        }
        case mac::LCID_APP_DTCH:
        case mac::LCID_APP_SIG:
        case mac::LCID_APP_VOICE:
        case mac::LCID_APP_VIDEO: {
            // M17: route the SDU to its QoS bearer's AM entity.
            const Qci qci = *qci_of_lcid(lcid);
            std::vector<uint8_t> am_pdu;
            if (up_sec_on_) {
                std::vector<uint8_t> inner;
                if (!pdcp::unprotect(up_key_, sdu, inner)) {
                    LOG_WARN(ev::SEC_DECRYPT_FAIL, {{"layer", "APP"}});
                    break;
                }
                am_pdu = pdcp::rx(rlc::tm_rx(inner));
            } else {
                am_pdu = pdcp::rx(rlc::tm_rx(sdu));
            }
            // M13/M17: the payload is an RLC data PDU for this bearer's
            // entity (AM for sig/best-effort, UM for voice/video).
            if (qci == Qci::SIG || qci == Qci::BEST_EFFORT) {
                auto& b = bearers_.am_of(qci);
                auto am_out = b.rx.rx(am_pdu);
                for (const auto& data : am_out.delivered) {
                    handle_app_sdu(data);
                }
                if (am_out.status_needed) {
                    auto status = b.rx.build_status();
                    LOG_INFO(ev::RLC_AM_STATUS_TX,
                             {{"dir", "ul"}, {"nacks", std::to_string(status[2])}});
                    enqueue_app_pdu(qci, status_lcid_of(qci), status);
                }
            } else {
                auto& b = bearers_.um_of(qci);
                b.rx.rx(now_ms_, am_pdu);
                for (const auto& data : b.rx.poll()) {
                    handle_app_sdu(data);
                }
            }
            break;
        }
        case mac::LCID_RLC_STATUS:
        case mac::LCID_RLC_STATUS_SIG: {
            // Retransmit whatever the BS reported missing (uplink AM), on
            // the matching bearer's queue.
            const Qci qci = *qci_of_status_lcid(lcid);
            auto& b = bearers_.am_of(qci);
            for (const auto& pdu : b.tx.on_status(now_ms_, sdu)) {
                enqueue_app_pdu(qci, lcid_of(qci),
                                pdcp::tx(rlc::tm_tx(pdu)));
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
        case mac::LCID_TPC: {
            // M19 closed-loop power control: BS-ordered relative adjustment.
            if (sdu.empty()) break;
            const int cmd = static_cast<int8_t>(sdu[0]);
            tpc_accum_db_ = std::clamp(tpc_accum_db_ + cmd, -15.0, 15.0);
            break;
        }
        default:
            break;
    }
}

// One reassembled app-bearer SDU (from AM rx or from a reorder-timeout
// resync): M16 U2U-framed SDUs are peer media, anything else is a loopback
// pong for the RTT accounting.
void UeNode::handle_app_sdu(const std::vector<uint8_t>& data) {
    app::U2uPacket pkt;
    if (app::decode_u2u(data, pkt)) {
        trace_pdu("APP", "RX", "u2u", data);
        handle_u2u(pkt);
        return;
    }
    trace_pdu("APP", "RX", "pong", data);
    handle_pong(data);
}

void UeNode::handle_pong(const std::vector<uint8_t>& data) {    if (data.size() < 4) return;
    uint32_t seq = static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[3]) << 24);
    auto it = app_tx_time_.find(seq);
    if (it != app_tx_time_.end()) ++app_rx_count_; // dedupe retx
    if (it == app_tx_time_.end()) return;
    last_app_rtt_ms_ = static_cast<int64_t>(now_ms_) - it->second;
    app_tx_time_.erase(it);
    ++rtt_samples_;
    rtt_sum_ms_ += last_app_rtt_ms_;
    rtt_min_ms_ = std::min(rtt_min_ms_, last_app_rtt_ms_);
    rtt_max_ms_ = std::max(rtt_max_ms_, last_app_rtt_ms_);
    LOG_INFO(ev::APP_RTT, {{"seq", std::to_string(seq)},
                          {"rtt_ms", std::to_string(last_app_rtt_ms_)}});
}

// ---- M16/M17: UE-to-UE media, SIP-lite dialogs, QoS bearers -------------------

void UeNode::send_u2u(const app::U2uPacket& pkt) {
    auto sdu = app::encode_u2u(pkt);
    trace_pdu("APP", "TX", "u2u", sdu);
    // M17: route to the QoS bearer of the packet's service class; the BS
    // mirrors the same LCID into the peer's downlink bearer. One-shots
    // (hangup, text, signaling) pass force so media congestion cannot
    // drop them; stream media and acks tolerate the refusal.
    const Qci qci = qci_of(pkt.kind);
    ensure_bearer(qci);
    if (qci == Qci::SIG || qci == Qci::BEST_EFFORT) {
        // One-shots (hangup, text, signaling) pass force so media
        // congestion cannot drop them; best-effort media tolerates it.
        const bool force = pkt.end || pkt.kind == app::MediaKind::MSG ||
                           pkt.kind == app::MediaKind::SIG;
        auto& b = bearers_.am_of(qci);
        for (const auto& am_pdu : b.tx.tx(now_ms_, sdu, force)) {
            enqueue_app_pdu(qci, lcid_of(qci), pdcp::tx(rlc::tm_tx(am_pdu)));
        }
    } else {
        // M17: voice/video ride UM — segments, no ARQ, no backpressure
        // window (the bearer queue itself bounds stale media).
        auto& b = bearers_.um_of(qci);
        for (const auto& um_pdu : b.tx.tx(sdu)) {
            enqueue_app_pdu(qci, lcid_of(qci), pdcp::tx(rlc::tm_tx(um_pdu)));
        }
    }
}

void UeNode::send_msg(const std::string& dst_imsi, const std::string& text) {
    if (!registered()) {
        LOG_WARN(ev::APP_TX_NO_CONTEXT, {});
        return;
    }
    maybe_wake_for_activity();
    app::U2uPacket pkt;
    pkt.kind = app::MediaKind::MSG;
    pkt.timestamp_ms = now_ms_;
    pkt.src_imsi = config_.imsi;
    pkt.dst_imsi = dst_imsi;
    pkt.payload.assign(text.begin(), text.end());
    LOG_INFO(ev::APP_MSG_TX, {{"dst", dst_imsi}, {"text", text}});
    send_u2u(pkt);
}

// ---- dialog lookup helpers -----------------------------------------------------

UeNode::CallDialog* UeNode::find_dialog(const std::string& peer,
                                        uint32_t call_id) {
    for (auto& d : dialogs_) {
        if (d.peer == peer && d.call_id == call_id) return &d;
    }
    return nullptr;
}

UeNode::CallDialog* UeNode::find_dialog_by_kind(app::MediaKind kind) {
    for (auto& d : dialogs_) {
        if (d.kind == kind && d.state == CallState::ESTABLISHED) return &d;
    }
    for (auto& d : dialogs_) {
        if (d.kind == kind) return &d;
    }
    return nullptr;
}

UeNode::CallDialog* UeNode::find_media_dialog(const std::string& peer,
                                              app::MediaKind kind) {
    for (auto& d : dialogs_) {
        if (d.peer == peer && d.kind == kind &&
            d.state == CallState::ESTABLISHED) {
            return &d;
        }
    }
    return nullptr;
}

// M18: any ESTABLISHED dialog belonging to this conference — conference
// media is accepted from every participant, not just the dialog peer.
UeNode::CallDialog* UeNode::find_conf_dialog(uint32_t conf_id) {
    for (auto& d : dialogs_) {
        if (d.conf_id == conf_id && d.state == CallState::ESTABLISHED) {
            return &d;
        }
    }
    return nullptr;
}

bool UeNode::has_conf_dialog(uint32_t conf_id) const {
    for (const auto& d : dialogs_) {
        if (d.conf_id == conf_id) return true;
    }
    return false;
}

UeNode::CallDialog* UeNode::find_incoming_ringing() {
    for (auto& d : dialogs_) {
        if (d.state == CallState::INCOMING_RINGING) return &d;
    }
    return nullptr;
}

UeNode::OutStream* UeNode::find_out_stream(const std::string& peer,
                                           app::MediaKind kind) {
    for (auto& s : out_streams_) {
        if (s.conf_id == 0 && s.peer == peer && s.kind == kind) return &s;
    }
    return nullptr;
}

UeNode::OutStream* UeNode::find_conf_out_stream(uint32_t conf_id) {
    for (auto& s : out_streams_) {
        if (s.conf_id == conf_id) return &s;
    }
    return nullptr;
}

UeNode::InStream* UeNode::find_in_stream(const std::string& peer,
                                         app::MediaKind kind,
                                         uint32_t conf_id) {
    for (auto& s : in_streams_) {
        if (s.peer == peer && s.kind == kind && s.conf_id == conf_id) {
            return &s;
        }
    }
    return nullptr;
}

// ---- dialog lifecycle -----------------------------------------------------------

void UeNode::start_call(app::MediaKind kind, const std::string& dst_imsi) {
    if (!registered()) {
        LOG_WARN(ev::APP_TX_NO_CONTEXT, {});
        return;
    }
    maybe_wake_for_activity();
    if (kind == app::MediaKind::MSG || kind == app::MediaKind::SIG) return;
    if (find_dialog_by_kind(kind) != nullptr) {
        // One media source per class — LOCAL busy: fail the new attempt
        // without touching the current dialog(s).
        last_call_fail_ = "busy";
        LOG_WARN(ev::SIP_CALL_FAILED, {{"peer", dst_imsi}, {"reason", "busy"}});
        return;
    }
    CallDialog d;
    d.state = CallState::OUTGOING_RINGING;
    d.peer = dst_imsi;
    d.kind = kind;
    d.call_id = next_call_id_++;
    dialogs_.push_back(d);
    LOG_INFO(ev::APP_CALL_START,
             {{"dst", dst_imsi}, {"kind", app::media_kind_name(kind)}});
    LOG_INFO(ev::SIP_INVITE_TX,
             {{"dst", dst_imsi}, {"kind", app::media_kind_name(kind)}});
    CallDialog& ref = dialogs_.back();
    send_sig(ref, app::SigMethod::INVITE);
    const uint32_t cid = ref.call_id;
    // No 180 within first_response_ms -> the callee is unreachable (the BS
    // echo-fallback returns our INVITE, which we ignore as our own).
    ref.first_response_timer = timers_.schedule(
        config_.first_response_ms, false, [this, dst_imsi, cid] {
            CallDialog* dp = find_dialog(dst_imsi, cid);
            if (dp == nullptr) return;
            dp->first_response_timer = 0;
            if (dp->state == CallState::OUTGOING_RINGING && !dp->got_180) {
                send_sig(*dp, app::SigMethod::CANCEL); // best effort
                fail_call(*dp, "unreachable");
            }
        });
    // Ringing forever -> CANCEL both sides.
    ref.noanswer_timer = timers_.schedule(
        config_.ring_timeout_ms, false, [this, dst_imsi, cid] {
            CallDialog* dp = find_dialog(dst_imsi, cid);
            if (dp == nullptr) return;
            dp->noanswer_timer = 0;
            if (dp->state == CallState::OUTGOING_RINGING) {
                send_sig(*dp, app::SigMethod::CANCEL);
                fail_call(*dp, "timeout");
            }
        });
}

void UeNode::answer() {
    CallDialog* d = find_incoming_ringing();
    if (d == nullptr) return;
    cancel_dialog_timers(*d);
    send_sig(*d, app::SigMethod::OK_200);
    d->state = CallState::ESTABLISHED; // media starts on the peer's ACK
}

// M18: dial two parties into one conference. Each party gets its own
// INVITE dialog (kind voice) carrying the shared conf_id; the BS splices
// membership together from the signaling it forwards and bridges media.
void UeNode::start_conf(const std::string& imsi_b, const std::string& imsi_c) {
    if (!registered()) {
        LOG_WARN(ev::APP_TX_NO_CONTEXT, {});
        return;
    }
    maybe_wake_for_activity();
    if (!dialogs_.empty()) {
        // Interop: a conference needs a free UE (and vice versa — peers
        // 486 our INVITEs while they are occupied).
        last_call_fail_ = "busy";
        LOG_WARN(ev::SIP_CALL_FAILED, {{"peer", imsi_b}, {"reason", "busy"}});
        return;
    }
    // Unique cell-wide without coordination: host C-RNTI in the high half.
    const uint32_t conf_id =
        (static_cast<uint32_t>(crnti_cache_) << 16) | next_conf_seq_++;
    for (const std::string& dst : {imsi_b, imsi_c}) {
        CallDialog d;
        d.state = CallState::OUTGOING_RINGING;
        d.peer = dst;
        d.kind = app::MediaKind::VOICE;
        d.call_id = next_call_id_++;
        d.conf_id = conf_id;
        dialogs_.push_back(d);
        LOG_INFO(ev::APP_CALL_START, {{"dst", dst}, {"kind", "conf"}});
        LOG_INFO(ev::SIP_INVITE_TX, {{"dst", dst}, {"kind", "conf"}});
        CallDialog& ref = dialogs_.back();
        send_sig(ref, app::SigMethod::INVITE);
        const uint32_t cid = ref.call_id;
        ref.first_response_timer = timers_.schedule(
            config_.first_response_ms, false, [this, dst, cid] {
                CallDialog* dp = find_dialog(dst, cid);
                if (dp == nullptr) return;
                dp->first_response_timer = 0;
                if (dp->state == CallState::OUTGOING_RINGING && !dp->got_180) {
                    send_sig(*dp, app::SigMethod::CANCEL); // best effort
                    fail_call(*dp, "unreachable");
                }
            });
        ref.noanswer_timer = timers_.schedule(
            config_.ring_timeout_ms, false, [this, dst, cid] {
                CallDialog* dp = find_dialog(dst, cid);
                if (dp == nullptr) return;
                dp->noanswer_timer = 0;
                if (dp->state == CallState::OUTGOING_RINGING) {
                    send_sig(*dp, app::SigMethod::CANCEL);
                    fail_call(*dp, "timeout");
                }
            });
    }
}

// Host: hang up every conference dialog (BYE per established party,
// CANCEL per still-ringing one) — the conference is over.
void UeNode::end_conf() {
    for (;;) { // end_dialog erases from dialogs_; re-scan each round
        CallDialog* d = nullptr;
        for (auto& x : dialogs_) {
            if (x.conf_id != 0) {
                d = &x;
                break;
            }
        }
        if (d == nullptr) return;
        end_dialog(*d);
    }
}

uint32_t UeNode::active_conf_id() const {
    for (const auto& d : dialogs_) {
        if (d.conf_id != 0) return d.conf_id;
    }
    return 0;
}


void UeNode::decline() {
    CallDialog* d = find_incoming_ringing();
    if (d == nullptr) return;
    send_sig(*d, app::SigMethod::DECLINE_603);
    LOG_INFO(ev::APP_CALL_END,
             {{"dst", d->peer}, {"kind", app::media_kind_name(d->kind)}});
    erase_dialog(*d);
}

// State-aware hangup of one dialog: BYE when established, CANCEL while our
// INVITE rings, 603 while the peer's INVITE rings.
void UeNode::end_dialog(CallDialog& d) {
    switch (d.state) {
        case CallState::IDLE:
            return;
        case CallState::OUTGOING_RINGING:
            send_sig(d, app::SigMethod::CANCEL);
            break;
        case CallState::INCOMING_RINGING:
            send_sig(d, app::SigMethod::DECLINE_603);
            break;
        case CallState::ESTABLISHED:
            LOG_INFO(ev::SIP_BYE_TX, {{"peer", d.peer}});
            send_sig(d, app::SigMethod::BYE);
            break;
    }
    LOG_INFO(ev::APP_CALL_END,
             {{"dst", d.peer}, {"kind", app::media_kind_name(d.kind)}});
    erase_dialog(d);
}

void UeNode::end_call() {
    CallDialog* d = nullptr;
    for (auto& x : dialogs_) {
        if (x.state == CallState::ESTABLISHED) { d = &x; break; }
    }
    if (d == nullptr) d = find_dialog_by_kind(app::MediaKind::VOICE);
    if (d == nullptr) d = find_dialog_by_kind(app::MediaKind::VIDEO);
    if (d != nullptr) end_dialog(*d);
}

void UeNode::end_call(app::MediaKind kind) {
    CallDialog* d = find_dialog_by_kind(kind);
    if (d == nullptr && dialogs_.size() == 1) d = &dialogs_.front();
    if (d != nullptr) end_dialog(*d);
}

void UeNode::stop_streams(bool notify) {
    while (!dialogs_.empty()) {
        CallDialog& d = dialogs_.front();
        if (notify) {
            // Best-effort signalling before the link dies (detach/abort).
            if (d.state == CallState::ESTABLISHED) {
                LOG_INFO(ev::SIP_BYE_TX, {{"peer", d.peer}});
                send_sig(d, app::SigMethod::BYE);
            } else if (d.state == CallState::OUTGOING_RINGING) {
                send_sig(d, app::SigMethod::CANCEL);
            } else { // INCOMING_RINGING
                send_sig(d, app::SigMethod::DECLINE_603);
            }
            LOG_INFO(ev::APP_CALL_END,
                     {{"dst", d.peer}, {"kind", app::media_kind_name(d.kind)}});
        }
        erase_dialog(d);
    }
    if (notify) {
        // M17: signalling is queued on the bearers now — flush before the
        // caller (detach/abort) resets the queues and HARQ state.
        pump_app_bearers();
    }
}

// Remove one dialog and its media streams without signalling anything.
void UeNode::erase_dialog(CallDialog& d) {
    cancel_dialog_timers(d);
    const std::string peer = d.peer;
    const app::MediaKind kind = d.kind;
    const uint32_t conf_id = d.conf_id;
    for (auto it = dialogs_.begin(); it != dialogs_.end(); ++it) {
        if (&*it == &d) {
            dialogs_.erase(it);
            break;
        }
    }
    // M18: the conference out-stream is shared by every dialog of the
    // conference — it dies with the LAST one; per-sender in-streams die
    // with their dialog peer, or all at once when the conference is over.
    const bool conf_survives =
        conf_id != 0 && has_conf_dialog(conf_id);
    for (auto it = out_streams_.begin(); it != out_streams_.end();) {
        const bool mine = conf_id != 0
                              ? (it->conf_id == conf_id && !conf_survives)
                              : (it->conf_id == 0 && it->peer == peer &&
                                 it->kind == kind);
        if (mine) {
            if (it->media_timer != 0) timers_.cancel(it->media_timer);
            it = out_streams_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = in_streams_.begin(); it != in_streams_.end();) {
        const bool mine = conf_id != 0
                              ? (it->conf_id == conf_id &&
                                 (it->peer == peer || !conf_survives))
                              : (it->conf_id == 0 && it->peer == peer &&
                                 it->kind == kind);
        if (mine) {
            it = in_streams_.erase(it);
        } else {
            ++it;
        }
    }
    maybe_stop_stream_timers();
    release_bearer_if_unused(qci_of(kind));
    release_bearer_if_unused(Qci::SIG);
}

void UeNode::fail_call(CallDialog& d, const char* reason) {
    last_call_fail_ = reason;
    LOG_WARN(ev::SIP_CALL_FAILED, {{"peer", d.peer}, {"reason", reason}});
    erase_dialog(d);
}

void UeNode::cancel_dialog_timers(CallDialog& d) {
    for (TimerId* t : {&d.first_response_timer, &d.noanswer_timer,
                       &d.autoanswer_timer}) {
        if (*t != 0) {
            timers_.cancel(*t);
            *t = 0;
        }
    }
}

// ---- M17: QoS bearer lifecycle ---------------------------------------------------

void UeNode::ensure_bearer(Qci qci) {
    if (qci == Qci::BEST_EFFORT) return; // default bearer always exists
    auto& est = bearers_.established_of(qci);
    if (est) return;
    est = true;
    LOG_INFO(ev::QOS_BEARER_SETUP,
             {{"c_rnti", std::to_string(crnti_cache_)},
              {"qci", std::to_string(static_cast<int>(qci))},
              {"kind", bearer_kind_name(qci)}});
}

void UeNode::release_bearer_if_unused(Qci qci) {
    if (qci == Qci::BEST_EFFORT) return;
    if (qci == Qci::SIG) {
        if (!dialogs_.empty()) return; // signalling continues
    } else {
        for (const auto& d : dialogs_) {
            if (qci_of(d.kind) == qci) return; // still in use
        }
    }
    auto& est = bearers_.established_of(qci);
    if (!est) return;
    est = false;
    LOG_INFO(ev::QOS_BEARER_TEARDOWN,
             {{"c_rnti", std::to_string(crnti_cache_)},
              {"qci", std::to_string(static_cast<int>(qci))},
              {"kind", bearer_kind_name(qci)}});
}

// ---- SIP-lite signaling I/O ------------------------------------------------------

void UeNode::send_sig(CallDialog& d, app::SigMethod method) {
    send_sig_to(method, d.peer, d.call_id, d.kind, d.conf_id);
}

void UeNode::send_sig_to(app::SigMethod method, const std::string& dst,
                         uint32_t call_id, app::MediaKind media,
                         uint32_t conf_id) {
    app::SigMessage msg;
    msg.method = method;
    msg.call_id = call_id;
    msg.media = media;
    msg.conf_id = conf_id;
    app::U2uPacket pkt;
    pkt.kind = app::MediaKind::SIG;
    pkt.timestamp_ms = now_ms_;
    pkt.src_imsi = config_.imsi;
    pkt.dst_imsi = dst;
    pkt.payload = app::encode_sig(msg);
    send_u2u(pkt); // force-flagged inside (SIG is one-shot control)
}

void UeNode::begin_media(CallDialog& d) {
    // Caller side, 200 OK received: ACK and open the media gates.
    send_sig(d, app::SigMethod::ACK);
    d.state = CallState::ESTABLISHED;
    cancel_dialog_timers(d);
    if (!d.established_log_done) {
        d.established_log_done = true;
        LOG_INFO(ev::SIP_CALL_ESTABLISHED,
                 {{"peer", d.peer},
                  {"kind", d.conf_id != 0 ? "conf"
                                          : app::media_kind_name(d.kind)}});
    }
    if (d.conf_id != 0) {
        ensure_conf_media(d);
        return;
    }
    ensure_bearer(qci_of(d.kind));
    OutStream s;
    s.kind = d.kind;
    s.peer = d.peer;
    const auto profile = app::media_profile(s.kind);
    const std::string peer = d.peer;
    const app::MediaKind kind = d.kind;
    s.media_timer = timers_.schedule(profile.interval_ms, true,
                                     [this, peer, kind] {
        OutStream* sp = find_out_stream(peer, kind);
        if (sp != nullptr) send_media_tick(*sp);
    });
    out_streams_.push_back(s);
    ensure_stream_timers();
}

// M18: every conference participant (host and parties alike) sends ONE
// voice stream to the BS bridge, created when the first conference dialog
// is established and shared by all dialogs of that conference.
void UeNode::ensure_conf_media(CallDialog& d) {
    if (find_conf_out_stream(d.conf_id) != nullptr) return;
    ensure_bearer(Qci::VOICE);
    OutStream s;
    s.kind = app::MediaKind::VOICE;
    s.peer = "conf"; // stats label; the wire dst is empty (bridge fan-out)
    s.conf_id = d.conf_id;
    const uint32_t conf_id = d.conf_id;
    // Bridge fan-out doubles the per-flow DL load (each party's downlink
    // carries TWO streams), so conference legs pace at half the 1:1 voice
    // rate — 3 x 16.7 pkt/s keeps every flow under the HARQ budget.
    const uint32_t interval_ms =
        app::media_profile(app::MediaKind::VOICE).interval_ms * 2;
    s.media_timer = timers_.schedule(interval_ms, true,
                                     [this, conf_id] {
        OutStream* sp = find_conf_out_stream(conf_id);
        if (sp != nullptr) send_media_tick(*sp);
    });
    out_streams_.push_back(s);
    ensure_stream_timers();
}

void UeNode::handle_sig(const std::string& src, const app::SigMessage& msg) {
    CallDialog* d = find_dialog(src, msg.call_id);
    switch (msg.method) {
        case app::SigMethod::INVITE: {
            if (d != nullptr) {
                // Duplicate/re-INVITE: idempotent response, keep the dialog.
                send_sig(*d, d->state == CallState::ESTABLISHED
                                 ? app::SigMethod::OK_200
                                 : app::SigMethod::RINGING_180);
                return;
            }
            if (!dialogs_.empty()) {
                // Occupied: 486 goes to the NEW caller, carrying ITS call_id
                // (and the conf_id, so the BS conference splice resolves the
                // refused invitation).
                send_sig_to(app::SigMethod::BUSY_486, src, msg.call_id,
                            msg.media, msg.conf_id);
                return;
            }
            CallDialog nd;
            nd.state = CallState::INCOMING_RINGING;
            nd.peer = src;
            nd.kind = msg.media;
            nd.call_id = msg.call_id;
            nd.conf_id = msg.conf_id;
            dialogs_.push_back(nd);
            LOG_INFO(ev::SIP_INVITE_RX,
                     {{"src", src}, {"kind", app::media_kind_name(msg.media)}});
            LOG_INFO(ev::APP_CALL_INCOMING,
                     {{"src", src}, {"kind", app::media_kind_name(msg.media)}});
            LOG_INFO(ev::SIP_RINGING_TX, {{"dst", src}});
            CallDialog& ref = dialogs_.back();
            send_sig(ref, app::SigMethod::RINGING_180);
            if (autoanswer_ms_ > 0) {
                const uint32_t cid = ref.call_id;
                ref.autoanswer_timer = timers_.schedule(
                    autoanswer_ms_, false, [this, src, cid] {
                        CallDialog* dp = find_dialog(src, cid);
                        if (dp == nullptr) return;
                        dp->autoanswer_timer = 0;
                        if (dp->state == CallState::INCOMING_RINGING) {
                            cancel_dialog_timers(*dp);
                            send_sig(*dp, app::SigMethod::OK_200);
                            dp->state = CallState::ESTABLISHED;
                        }
                    });
            }
            return;
        }
        case app::SigMethod::RINGING_180:
            if (d != nullptr && d->state == CallState::OUTGOING_RINGING) {
                d->got_180 = true;
                if (d->first_response_timer != 0) {
                    timers_.cancel(d->first_response_timer);
                    d->first_response_timer = 0;
                }
                LOG_INFO(ev::SIP_RINGING_RX, {{"src", src}});
            }
            return;
        case app::SigMethod::OK_200:
            if (d != nullptr && d->state == CallState::OUTGOING_RINGING) {
                begin_media(*d);
            }
            return;
        case app::SigMethod::ACK:
            if (d != nullptr && d->state == CallState::ESTABLISHED &&
                !d->established_log_done) {
                // Callee: media may now flow in both directions.
                d->established_log_done = true;
                LOG_INFO(ev::SIP_CALL_ESTABLISHED,
                         {{"peer", src},
                          {"kind", d->conf_id != 0
                                       ? "conf"
                                       : app::media_kind_name(d->kind)}});
                // M18: a conference party also TALKS — open its stream to
                // the bridge (the caller does this in begin_media).
                if (d->conf_id != 0) ensure_conf_media(*d);
            }
            return;
        case app::SigMethod::BYE:
            if (d != nullptr && d->state == CallState::ESTABLISHED) {
                send_sig(*d, app::SigMethod::OK_BYE);
                LOG_INFO(ev::SIP_BYE_RX, {{"peer", src}});
                LOG_INFO(ev::APP_CALL_PEER_END,
                         {{"src", src},
                          {"kind", app::media_kind_name(d->kind)}});
                erase_dialog(*d);
            } else if (d == nullptr) {
                // Stray BYE for a dialog we don't have: ack to the SENDER
                // and ignore.
                send_sig_to(app::SigMethod::OK_BYE, src, msg.call_id,
                            msg.media);
            }
            return;
        case app::SigMethod::OK_BYE:
            return; // we already tore down when we sent BYE
        case app::SigMethod::BUSY_486:
            if (d != nullptr && d->state == CallState::OUTGOING_RINGING) {
                fail_call(*d, "busy");
            }
            return;
        case app::SigMethod::DECLINE_603:
            if (d != nullptr && d->state == CallState::OUTGOING_RINGING) {
                fail_call(*d, "declined");
            }
            return;
        case app::SigMethod::CANCEL:
            if (d != nullptr && d->state == CallState::INCOMING_RINGING) {
                LOG_INFO(ev::SIP_CALL_FAILED,
                         {{"peer", src}, {"reason", "cancel"}});
                LOG_INFO(ev::APP_CALL_PEER_END,
                         {{"src", src},
                          {"kind", app::media_kind_name(d->kind)}});
                erase_dialog(*d);
            }
            return;
    }
}

// ---- media streams ------------------------------------------------------------------

void UeNode::send_media_tick(OutStream& s) {
    app::U2uPacket pkt;
    pkt.kind = s.kind;
    pkt.seq = ++s.seq;
    pkt.timestamp_ms = now_ms_;
    pkt.src_imsi = config_.imsi;
    pkt.dst_imsi = s.conf_id != 0 ? "" : s.peer; // conf media targets the bridge
    pkt.conf_id = s.conf_id;
    pkt.payload = app::make_media_payload(s.kind, pkt.seq);
    s.ack_pending[pkt.seq] = now_ms_;
    ++s.tx;
    send_u2u(pkt);
}

void UeNode::handle_u2u(const app::U2uPacket& pkt) {
    // The BS echoes U2U packets with an unknown destination back to the
    // sender (legacy behaviour); never consume our own media.
    if (pkt.src_imsi == config_.imsi) return;
    if (pkt.ack) {
        // RTCP-like feedback for one of our outgoing streams. Conference
        // streams take acks from EVERY participant (first ack wins the RTT
        // sample; the sweep treats any later ack as proof of delivery).
        OutStream* sp = pkt.conf_id != 0 ? find_conf_out_stream(pkt.conf_id)
                                         : find_out_stream(pkt.src_imsi, pkt.kind);
        if (sp == nullptr) return;
        auto& s = *sp;
        // The peer acks every media packet it receives, in order — so any
        // ack proves delivery of everything below it too (used by the
        // sweep to tell "media lost" apart from "ack lost/late").
        s.max_ack_seq = std::max(s.max_ack_seq, pkt.seq);
        auto it = s.ack_pending.find(pkt.seq);
        if (it == s.ack_pending.end()) return; // duplicate or already swept
        const int64_t rtt = static_cast<int64_t>(now_ms_) - it->second;
        s.ack_pending.erase(it);
        ++s.rx_ack;
        s.rtt_sum += rtt;
        ++s.rtt_n;
        return;
    }
    if (pkt.end) {
        // Legacy M16 stream-teardown flag (no longer sent; BYE replaces it).
        InStream* isp = find_in_stream(pkt.src_imsi, pkt.kind);
        if (isp != nullptr) {
            LOG_INFO(ev::APP_CALL_PEER_END,
                     {{"src", pkt.src_imsi},
                      {"kind", app::media_kind_name(isp->kind)}});
            for (auto it = in_streams_.begin(); it != in_streams_.end(); ++it) {
                if (&*it == isp) {
                    in_streams_.erase(it);
                    break;
                }
            }
            maybe_stop_stream_timers();
        }
        return;
    }
    if (pkt.kind == app::MediaKind::MSG) {
        ++msg_rx_count_;
        last_msg_src_ = pkt.src_imsi;
        last_msg_text_.assign(pkt.payload.begin(), pkt.payload.end());
        LOG_INFO(ev::APP_MSG_RX,
                 {{"src", pkt.src_imsi}, {"text", last_msg_text_}});
        return;
    }
    if (pkt.kind == app::MediaKind::SIG) {
        app::SigMessage msg;
        if (app::decode_sig(pkt.payload, msg)) handle_sig(pkt.src_imsi, msg);
        return; // undecodable/unknown-method sig: forward-compat ignore
    }
    // Voice/video media packet — only inside an established dialog. For
    // conference media (conf_id != 0) any ESTABLISHED dialog of that
    // conference accepts it — the sender is a fellow participant, not
    // necessarily our dialog peer. Anything else (media racing ahead of
    // the ACK, strays after teardown) is dropped here, NOT turned into an
    // implicit call like in M16.
    CallDialog* dp = pkt.conf_id != 0
                         ? find_conf_dialog(pkt.conf_id)
                         : find_media_dialog(pkt.src_imsi, pkt.kind);
    if (dp == nullptr) return;
    InStream* isp = find_in_stream(pkt.src_imsi, pkt.kind, pkt.conf_id);
    if (isp == nullptr) {
        InStream s;
        s.kind = pkt.kind;
        s.peer = pkt.src_imsi;
        s.conf_id = pkt.conf_id;
        s.expected_seq = pkt.seq; // first seen packet anchors the gap count
        in_streams_.push_back(s);
        isp = &in_streams_.back();
        ensure_stream_timers();
        ensure_bearer(qci_of(pkt.kind));
        if (!dp->established_log_done) {
            // Media is ACK-gated on the caller, so arriving media PROVES the
            // dialog is established — log it here too. The explicit ACK may
            // have been sacrificed to an RLC reorder skip under congestion.
            dp->established_log_done = true;
            LOG_INFO(ev::SIP_CALL_ESTABLISHED,
                     {{"peer", dp->peer},
                      {"kind", dp->conf_id != 0
                                   ? "conf"
                                   : app::media_kind_name(dp->kind)}});
            // M18: the ACK died but we ARE established — a conference party
            // must still open its own stream to the bridge.
            if (dp->conf_id != 0) ensure_conf_media(*dp);
        }
    }
    auto& s = *isp;
    if (pkt.seq >= s.expected_seq) {
        s.loss += pkt.seq - s.expected_seq;
        s.expected_seq = pkt.seq + 1;
    } // else: reordered/duplicate (AM is in-order, so this is defensive)
    ++s.rx;
    // ACK back to the sender (tiny packet echoing the media seq).
    app::U2uPacket ack;
    ack.kind = pkt.kind;
    ack.ack = true;
    ack.seq = pkt.seq;
    ack.timestamp_ms = now_ms_;
    ack.src_imsi = config_.imsi;
    ack.dst_imsi = pkt.src_imsi;
    ack.conf_id = pkt.conf_id;
    ++s.ack_tx;
    send_u2u(ack);
}

void UeNode::ensure_stream_timers() {
    if (stream_stats_timer_ == 0) {
        stream_stats_timer_ = timers_.schedule(5000, true,
                                               [this] { emit_stream_stats(); });
    }
    if (stream_sweep_timer_ == 0) {
        stream_sweep_timer_ = timers_.schedule(500, true,
                                               [this] { sweep_stream_acks(); });
    }
}

void UeNode::maybe_stop_stream_timers() {
    if (!out_streams_.empty() || !in_streams_.empty()) return;
    for (TimerId* t : {&stream_stats_timer_, &stream_sweep_timer_}) {
        if (*t != 0) {
            timers_.cancel(*t);
            *t = 0;
        }
    }
}

void UeNode::sweep_stream_acks() {
    // An ack missing well beyond any legitimate RTT is resolved one of two
    // ways: a later ack already proved the peer received that media packet
    // (only the ack died on the return path -> delivered, not loss), or
    // nothing newer ever came back (-> media loss).
    constexpr uint32_t kAckWindowMs = 3000;
    for (auto& s : out_streams_) {
        for (auto it = s.ack_pending.begin(); it != s.ack_pending.end();) {
            if (static_cast<int32_t>(now_ms_ - it->second) >=
                static_cast<int32_t>(kAckWindowMs)) {
                if (it->first <= s.max_ack_seq) {
                    ++s.rx_ack; // proven delivered by a later ack
                } else {
                    ++s.loss;
                }
                it = s.ack_pending.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void UeNode::emit_stream_stats() {
    // M18: conference streams report kind "conf" (they ride the voice QCI)
    // plus the conf_id so the UI can group participants of one conference.
    for (const auto& s : out_streams_) {
        std::map<std::string, std::string> f{
            {"kind", s.conf_id != 0 ? "conf" : app::media_kind_name(s.kind)},
            {"peer", s.peer},
            {"tx", std::to_string(s.tx)},
            {"rx", std::to_string(s.rx_ack)},
            {"loss", std::to_string(s.loss)},
            {"rtt_avg", std::to_string(s.rtt_n ? s.rtt_sum / s.rtt_n : -1)},
            {"qci", std::to_string(static_cast<int>(qci_of(s.kind)))}};
        if (s.conf_id != 0) f["conf_id"] = std::to_string(s.conf_id);
        LOG_INFO(ev::APP_STREAM_STATS, f);
    }
    for (const auto& s : in_streams_) {
        std::map<std::string, std::string> f{
            {"kind", s.conf_id != 0 ? "conf" : app::media_kind_name(s.kind)},
            {"peer", s.peer},
            {"tx", std::to_string(s.ack_tx)},
            {"rx", std::to_string(s.rx)},
            {"loss", std::to_string(s.loss)},
            {"rtt_avg", "-1"}, // RX side has no RTT samples
            {"qci", std::to_string(static_cast<int>(qci_of(s.kind)))}};
        if (s.conf_id != 0) f["conf_id"] = std::to_string(s.conf_id);
        LOG_INFO(ev::APP_STREAM_STATS, f);
    }
}

// ---- M16/M17 observability (aggregate over concurrent streams/dialogs) ---------

int UeNode::call_state() const {
    int best = 0;
    for (const auto& d : dialogs_) {
        const int s = static_cast<int>(d.state);
        if (s == 3) return 3;
        if (s == 2) {
            best = 2;
        } else if (s == 1 && best == 0) {
            best = 1;
        }
    }
    return best;
}

std::string UeNode::call_peer() const {
    const CallDialog* best = nullptr;
    for (const auto& d : dialogs_) {
        if (d.state == CallState::ESTABLISHED) return d.peer;
        if (d.state == CallState::INCOMING_RINGING) best = &d;
        else if (best == nullptr) best = &d;
    }
    return best != nullptr ? best->peer : std::string();
}

bool UeNode::call_established_logged() const {
    for (const auto& d : dialogs_) {
        if (d.established_log_done) return true;
    }
    return false;
}

bool UeNode::bearer_established(int qci) const {
    return bearers_.established_of(static_cast<Qci>(qci));
}

uint32_t UeNode::stream_tx_count() const {
    uint32_t n = 0;
    for (const auto& s : out_streams_) n += s.tx;
    return n;
}

uint32_t UeNode::stream_rx_count() const {
    uint32_t n = 0;
    for (const auto& s : in_streams_) n += s.rx;
    return n;
}

uint32_t UeNode::stream_loss_count() const {
    uint32_t n = 0;
    for (const auto& s : out_streams_) n += s.loss;
    for (const auto& s : in_streams_) n += s.loss;
    return n;
}

int64_t UeNode::stream_rtt_avg_ms() const {
    int64_t sum = 0;
    uint32_t n = 0;
    for (const auto& s : out_streams_) {
        sum += s.rtt_sum;
        n += s.rtt_n;
    }
    return n != 0 ? sum / n : -1;
}

uint32_t UeNode::stream_rx_count(app::MediaKind kind) const {
    uint32_t n = 0;
    for (const auto& s : in_streams_) {
        if (s.kind == kind) n += s.rx;
    }
    return n;
}

uint32_t UeNode::stream_rx_from(const std::string& peer) const {
    uint32_t n = 0;
    for (const auto& s : in_streams_) {
        if (s.peer == peer) n += s.rx;
    }
    return n;
}

uint32_t UeNode::stream_loss_count(app::MediaKind kind) const {
    uint32_t n = 0;
    for (const auto& s : out_streams_) {
        if (s.kind == kind) n += s.loss;
    }
    for (const auto& s : in_streams_) {
        if (s.kind == kind) n += s.loss;
    }
    return n;
}

int64_t UeNode::stream_rtt_avg_ms(app::MediaKind kind) const {
    int64_t sum = 0;
    uint32_t n = 0;
    for (const auto& s : out_streams_) {
        if (s.kind != kind) continue;
        sum += s.rtt_sum;
        n += s.rtt_n;
    }
    return n != 0 ? sum / n : -1;
}

std::string UeNode::incoming_peer() const {
    return in_streams_.empty() ? std::string() : in_streams_.front().peer;
}

uint32_t UeNode::ack_rx_count() const {
    uint32_t n = 0;
    for (const auto& s : out_streams_) n += s.rx_ack;
    return n;
}

void UeNode::uplink_send(uint8_t lcid, const std::vector<uint8_t>& sdu_in) {
    std::vector<uint8_t> sdu = sdu_in;
    // M17: all app bearers share one PDCP sequence space (nonce stays
    // unique; per-bearer COUNT is not modelled — see docs/m17_plan.md).
    const bool ciphered =
        up_sec_on_ && (lcid == mac::LCID_NAS_DCCH ||
                       qci_of_lcid(lcid).has_value());
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

// ---- M14: mobility ----------------------------------------------------------

void UeNode::handle_ccch_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu) {
    auto msg = rrc::RrcMessage::decode(sdu);
    switch (msg.msg_type) {
        case rrc::RrcMessageType::HO_COMMAND: {
            // [target_cell:2][new_crnti:2]
            if (msg.value.size() < 4) break;
            uint16_t target = static_cast<uint16_t>(msg.value[0] |
                                                    (msg.value[1] << 8));
            uint16_t new_crnti = static_cast<uint16_t>(msg.value[2] |
                                                       (msg.value[3] << 8));
            if (cells_.count(target) == 0) {
                LOG_WARN(ev::HO_TRIGGERED,
                         {{"to_cell", std::to_string(target)},
                          {"result", "unknown_cell"}});
                break;
            }
            apply_handover(target, new_crnti);
            break;
        }
        case rrc::RrcMessageType::REESTABLISHMENT_OK: {
            if (!reestablish_pending_ || msg.value.size() < 2) break;
            uint16_t new_crnti = static_cast<uint16_t>(msg.value[0] |
                                                       (msg.value[1] << 8));
            reestablish_pending_ = false;
            crnti_cache_ = new_crnti;
            rrc_ue_.restore_connected(new_crnti);
            harq_rx_.reset(); // fresh connection: no cross-attach soft memory
            LOG_INFO(ev::RRC_REEST_OK, {{"old", std::to_string(pre_rlf_crnti_)},
                                        {"new", std::to_string(new_crnti)}});
            break;
        }
        case rrc::RrcMessageType::REESTABLISHMENT_FAILURE:
            if (reestablish_pending_) reestablish_failed("context_gone");
            break;
        default:
            // Legacy signalling: SETUP / RELEASE / etc.
            rrc_ue_.on_message(sdu);
            harq_rx_.reset(); // fresh connection: no cross-attach soft memory
            // Connection is up: proceed with the NAS attach if one is pending.
            if (attach_requested_ &&
                nas_ue_.state() == nas::UeState::DEREGISTERED &&
                rrc_ue_.state() == rrc::UeState::CONNECTED) {
                nas_ue_.send_attach_request(config_.imsi);
                start_measurements();
            }
            break;
    }
    (void)lcid;
}

void UeNode::start_measurements() {
    if (meas_timer_ != 0 || config_.meas_period_ms == 0) return;
    meas_timer_ = timers_.schedule(config_.meas_period_ms, true, [this] {
        if (rrc_ue_.state() == rrc::UeState::CONNECTED && !cells_.empty()) {
            send_measurement_report();
        }
    });
}

void UeNode::stop_measurements() {
    if (meas_timer_ != 0) {
        timers_.cancel(meas_timer_);
        meas_timer_ = 0;
    }
}

void UeNode::send_measurement_report() {
    // [n:1]{[cell_id:2][strength:1]} — strength = SIB RX count so far.
    // Only currently-audible cells are reported; a cell whose broadcasts
    // went silent drops out of the report after kCellStaleMs.
    constexpr uint32_t kCellStaleMs = 800;
    std::vector<std::pair<uint16_t, uint32_t>> audible;
    for (const auto& [cell_id, c] : cells_) {
        if (now_ms_ - c.last_seen_ms <= kCellStaleMs) {
            audible.emplace_back(cell_id, c.rx_count);
        }
    }
    std::vector<uint8_t> value;
    value.push_back(static_cast<uint8_t>(audible.size()));
    for (const auto& [cell_id, rx] : audible) {
        value.push_back(static_cast<uint8_t>(cell_id & 0xFF));
        value.push_back(static_cast<uint8_t>((cell_id >> 8) & 0xFF));
        value.push_back(static_cast<uint8_t>(std::min<uint32_t>(rx, 255)));
    }
    rrc::RrcMessage report;
    report.msg_type = rrc::RrcMessageType::MEAS_REPORT;
    report.value = value;
    uplink_send(mac::LCID_CCCH, report.encode());
    LOG_INFO(ev::MEAS_REPORT_TX,
             {{"serving", std::to_string(serving_cell_)},
              {"n", std::to_string(audible.size())}});
}

void UeNode::apply_handover(uint16_t target_cell, uint16_t new_crnti) {
    LOG_INFO(ev::HO_COMMAND_TX,
             {{"cell", std::to_string(target_cell)},
              {"rnti", std::to_string(new_crnti)}});
    serving_cell_ = target_cell;
    crnti_cache_ = new_crnti;
    rrc_ue_.restore_connected(new_crnti);
    // Air-side state restarts on the new cell; NAS registration and the
    // security context survive (the target received them at preparation).
    harq_tx_.reset();
    harq_rx_.reset();
    for (Qci qci : {Qci::SIG, Qci::BEST_EFFORT}) {
        auto& b = bearers_.am_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
    }
    for (Qci qci : {Qci::VOICE, Qci::VIDEO}) {
        auto& b = bearers_.um_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
    }
    pdcp_seq_ = 0;

    // Confirm to the network. Both cells hear this; only the target has a
    // matching prepared context.
    rrc::RrcMessage done;
    done.msg_type = rrc::RrcMessageType::HO_COMPLETE;
    done.value = {static_cast<uint8_t>(new_crnti & 0xFF),
                  static_cast<uint8_t>((new_crnti >> 8) & 0xFF)};
    uplink_send(mac::LCID_CCCH, done.encode());
}

void UeNode::declare_rlf() {
    LOG_ERROR(ev::RLF_DETECTED,
              {{"crnti", std::to_string(crnti_cache_)}});
    pre_rlf_crnti_ = crnti_cache_;
    reestablish_pending_ = true;
    // Air side dies with the link; NAS registration and security survive.
    // Traffic keeps running: pings buffer in the AM/HARQ machinery and
    // drain once the link is re-established (session continuity).
    harq_tx_.reset();
    harq_rx_.reset();
    for (Qci qci : {Qci::SIG, Qci::BEST_EFFORT}) {
        auto& b = bearers_.am_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
    }
    for (Qci qci : {Qci::VOICE, Qci::VIDEO}) {
        auto& b = bearers_.um_of(qci);
        b.tx.reset();
        b.rx.reset();
        b.queue.clear();
    }
    rach_ue_.force_idle();
    rrc_ue_.force_idle();
    crnti_cache_ = 0;

    // Re-synchronise through RACH; MSG3 carries the re-establishment request
    // instead of an RRC SetupRequest.
    rrc::RrcMessage req;
    req.msg_type = rrc::RrcMessageType::REESTABLISHMENT_REQUEST;
    req.value = {static_cast<uint8_t>(pre_rlf_crnti_ & 0xFF),
                 static_cast<uint8_t>((pre_rlf_crnti_ >> 8) & 0xFF),
                 static_cast<uint8_t>(serving_cell_ & 0xFF),
                 static_cast<uint8_t>((serving_cell_ >> 8) & 0xFF)};
    reest_req_pdu_ = req.encode();
    LOG_INFO(ev::RRC_REEST_REQ_TX,
             {{"old_crnti", std::to_string(pre_rlf_crnti_)}});
    pending_ccch_ = reest_req_pdu_;

    if (reest_guard_ != 0) timers_.cancel(reest_guard_);
    reest_guard_ = timers_.schedule(config_.attach_guard_ms, false, [this] {
        reest_guard_ = 0;
        if (reestablish_pending_) reestablish_failed("guard_timeout");
    });

    attach_requested_ = true; // fallback path if re-establishment fails
    rach_ue_.start_rach();
}

void UeNode::reestablish_failed(const char* reason) {
    LOG_WARN(ev::RRC_REEST_FAIL,
             {{"reason", reason},
              {"c_rnti", std::to_string(pre_rlf_crnti_)}});
    reestablish_pending_ = false;
    nas_ue_.force_deregistered();
    up_sec_on_ = false;
    pdcp_seq_ = 0;
    crnti_cache_ = 0;
    rach_ue_.force_idle();
    rrc_ue_.force_idle();
    maybe_start_attach(); // full re-attach with a fresh registration
}

}

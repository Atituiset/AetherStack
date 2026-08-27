#ifndef AETHER_CORE_UE_NODE_H
#define AETHER_CORE_UE_NODE_H

#include "core/radio_frames.h"
#include "app/u2u.h"
#include "common/crypto.h"
#include "core/harq.h"
#include "core/qos.h"
#include "core/timer_list.h"
#include "mac/rach_ue.h"
#include "nas/nas_ue.h"
#include "rlc/rlc_am.h"
#include "rrc/rrc_ue.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

// Tunable knobs for the UE node. Defaults match the live demo scenario.
struct UeNodeConfig {
    std::string imsi = "460011234567890";
    mac::RachConfig rach; // preamble/retries; windows come from the fields below
    uint32_t rar_window_ms = 250;
    uint32_t cr_window_ms = 500;
    uint32_t attach_guard_ms = 3000;
    uint32_t backoff_min_ms = 100;
    uint32_t backoff_max_ms = 300;
    uint32_t rng_seed = 42;
    // M14: mobility knobs.
    uint32_t meas_period_ms = 500;      // measurement report interval
    uint32_t radio_link_failure_ms = 0; // 0 disables RLF detection
    // M17: SIP-lite dialog timeouts (tests shorten these).
    uint32_t first_response_ms = 6000;  // no 180 after INVITE -> unreachable
    uint32_t ring_timeout_ms = 20000;   // ringing too long -> CANCEL
};

// UE-side protocol stack orchestration. Owns every layer entity, encodes and
// decodes air frames, drives timers. The owner (process main or a test)
// provides the radio: modulatable bit bursts out via the send callback,
// decoded bits back in via on_air_bits().
class UeNode {
public:
    using AirBitsSend = std::function<void(const std::vector<uint8_t>&)>;

    explicit UeNode(const UeNodeConfig& config = UeNodeConfig{});

    void set_air_send(AirBitsSend send);

    // Ingest bits decoded from one received radio burst.
    void on_air_bits(const std::vector<uint8_t>& bits);

    // M19: same ingest plus per-burst radio metrics from the shell (DMRS
    // SNR estimate + mean burst power). Feeds CQI reporting, open-loop TX
    // power and the LINK_QUALITY telemetry.
    void on_air_bits_with_metrics(const std::vector<uint8_t>& bits,
                                  float snr_db, float pwr_dbm);

    // Advance the timer table to now_ms (monotonic ms since process start).
    void tick(uint32_t now_ms);

    // Commands (wired to the control channel in the process main).
    void attach();  // SIB-gated: starts RACH + RRC once system info is known
    void detach();  // NAS detach + RRC release
    // M20: RRC_INACTIVE. sleep() asks the network to suspend us (context,
    // keys, bearers and NAS registration are kept); wake() resumes via
    // RACH + RRCResumeRequest — no RRC setup, no NAS re-attach. Outbound
    // activity (send/msg/call/traffic) wakes implicitly.
    void sleep();
    void wake();
    bool inactive() const { return rrc_ue_.inactive(); }
    void send_app_data(const std::vector<uint8_t>& payload);

    // Continuous user-plane loopback (M7.1): sends one small packet every
    // traffic_interval_ms while registered. Stopped by stop_traffic(),
    // detach() and attach aborts.
    void start_traffic(uint32_t interval_ms = 100);
    void stop_traffic();
    bool traffic_running() const { return traffic_timer_ != 0; }

    // ---- M16/M17: UE-to-UE media + SIP-lite call control -----------------------
    // One-shot text message to a peer UE (IMSI acts as the phone number).
    void send_msg(const std::string& dst_imsi, const std::string& text);
    // Dial a peer: sends a SIP-lite INVITE. Media only starts once the
    // dialog is established (peer answers with 200 OK, we ACK). Multiple
    // dialogs of DIFFERENT kinds may be active at once (e.g. a voice and a
    // video call); a second dialog of the same kind fails locally "busy".
    void start_call(app::MediaKind kind, const std::string& dst_imsi);
    void answer();  // callee: accept a ringing incoming call (sends 200 OK)
    void decline(); // callee: reject a ringing incoming call (sends 603)
    // Hang up: BYE when established, CANCEL while our INVITE is ringing,
    // decline while the peer's INVITE is ringing. With several dialogs,
    // end_call() ends the most relevant one; end_call(kind) ends the
    // dialog of that media kind (falling back to the only active dialog).
    void end_call();
    void end_call(app::MediaKind kind);
    // M18: 3-party conference (voice). The host dials both parties with
    // INVITEs carrying a shared conf_id; the BS bridges every participant's
    // media to all the others. `conf end` (host) hangs up every conference
    // dialog; a participant's end_call() leaves just that party.
    void start_conf(const std::string& imsi_b, const std::string& imsi_c);
    void end_conf();
    uint32_t active_conf_id() const; // 0 when no conference dialog exists
    bool call_active() const { return !out_streams_.empty(); }
    // Auto-answer a ringing incoming call after `ms` (0 = manual only).
    void set_autoanswer(uint32_t ms) { autoanswer_ms_ = ms; }

    // Call dialog states (call_state() values for tests/status):
    // 0 idle, 1 outgoing ringing (we INVITEd), 2 incoming ringing,
    // 3 established (media may flow). With several dialogs the
    // highest-precedence state wins (3 > 2 > 1).
    int call_state() const;
    std::string call_peer() const;
    // True once SIP_CALL_ESTABLISHED was emitted for an active dialog.
    bool call_established_logged() const;
    const std::string& last_call_fail_reason() const { return last_call_fail_; }
    // M17: QoS dedicated bearer observability (tests).
    bool bearer_established(int qci) const;

    // M16 observability (status command / tests)
    uint32_t msg_rx_count() const { return msg_rx_count_; }
    const std::string& last_msg_src() const { return last_msg_src_; }
    const std::string& last_msg_text() const { return last_msg_text_; }
    uint32_t stream_tx_count() const;
    uint32_t stream_rx_count() const;
    uint32_t stream_loss_count() const;
    int64_t stream_rtt_avg_ms() const;
    // Per-media-kind variants (M17 QoS contention tests).
    uint32_t stream_rx_count(app::MediaKind kind) const;
    uint32_t stream_loss_count(app::MediaKind kind) const;
    int64_t stream_rtt_avg_ms(app::MediaKind kind) const;
    // M18: media received from one specific peer (conference fan-in tests).
    uint32_t stream_rx_from(const std::string& peer) const;
    bool incoming_call_active() const { return !in_streams_.empty(); }
    std::string incoming_peer() const;
    uint32_t ack_rx_count() const;

    // Observability
    mac::RachState mac_state() const { return rach_ue_.state(); }
    rrc::UeState rrc_state() const { return rrc_ue_.state(); }
    nas::UeState nas_state() const { return nas_ue_.state(); }
    nas::NasUe& nas() { return nas_ue_; }   // test/USIM provisioning access
    uint16_t crnti() const { return crnti_cache_; }
    uint16_t serving_cell() const { return serving_cell_; }  // M14
    bool registered() const { return nas_ue_.state() == nas::UeState::REGISTERED; }
    bool has_system_info() const { return mib_ok_ && sib1_ok_; }

    // ---- M19: link adaptation / TX power observability --------------------
    float dl_snr_db() const { return dl_snr_ewma_; }
    float rsrp_dbm() const { return rsrp_ewma_; }
    int current_cqi() const;              // 1..15, -1 without measurements
    double tx_power_db() const { return tx_power_db_; }
    double tx_gain() const;               // linear amplitude for the radio shell
    uint32_t app_tx_count() const { return app_tx_seq_; }
    uint32_t app_rx_count() const { return app_rx_count_; }
    uint32_t app_loss_count() const { return app_loss_count_; }
    int64_t last_app_rtt_ms() const { return last_app_rtt_ms_; }
    // Aggregated RTT over the whole run (ms); count == answered pings.
    uint32_t rtt_sample_count() const { return rtt_samples_; }
    int64_t rtt_min_ms() const { return rtt_min_ms_; }
    int64_t rtt_max_ms() const { return rtt_max_ms_; }
    int64_t rtt_avg_ms() const {
        return rtt_samples_ ? rtt_sum_ms_ / rtt_samples_ : -1;
    }

    // Emit one TRAFFIC_STATS line on demand (also used by the stats command).
    void emit_traffic_stats();

private:
    void handle_air_frame(const AirFrame& frame);
    void handle_rach_payload(AirFrameType type, uint16_t rnti,
                             const std::vector<uint8_t>& payload);
    void handle_data_pdu(uint16_t rnti, const std::vector<uint8_t>& pdu);
    void handle_sysinfo_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu,
                            bool broadcast);
    void handle_dedicated_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu);
    void handle_pong(const std::vector<uint8_t>& data); // RTT accounting
    void handle_app_sdu(const std::vector<uint8_t>& data); // u2u vs pong dispatch

    // ---- M16: UE-to-UE media -------------------------------------------------
    // Outgoing stream (we are the caller). Acks come back as U2U packets
    // with the ACK flag and give us RTT; loss = acks missing past 3 s.
    // One stream per (peer, kind) — concurrent dialogs of different kinds
    // are supported (M17).
    struct OutStream {
        app::MediaKind kind;
        std::string peer;
        uint32_t conf_id = 0; // M18: != 0 -> one stream to the BS bridge
        uint32_t seq = 0;
        uint32_t tx = 0;
        uint32_t rx_ack = 0;
        uint32_t loss = 0;
        int64_t rtt_sum = 0;
        uint32_t rtt_n = 0;
        uint32_t max_ack_seq = 0; // peer got everything up to here (acks are in-order)
        std::unordered_map<uint32_t, uint32_t> ack_pending; // seq -> tx ms
        TimerId media_timer = 0;
    };
    // Incoming stream (a peer called us). Loss = sequence gaps in the media
    // stream; every media packet is answered with a tiny ACK.
    struct InStream {
        app::MediaKind kind;
        std::string peer;
        uint32_t conf_id = 0; // M18: conference this media belongs to
        uint32_t rx = 0;
        uint32_t loss = 0;
        uint32_t ack_tx = 0;
        uint32_t expected_seq = 0;
    };
    std::vector<OutStream> out_streams_;
    std::vector<InStream> in_streams_;
    TimerId stream_stats_timer_ = 0;
    TimerId stream_sweep_timer_ = 0;

    // ---- M17: SIP-lite call dialogs + QoS bearers -----------------------------
    // Media (streams above) is gated on the ESTABLISHED state; the M16-era
    // END-flag packet is still decoded for tolerance but no longer sent
    // (BYE replaces it). Multiple dialogs may be active (one per kind on
    // the calling side, one incoming at a time).
    enum class CallState : int {
        IDLE = 0,
        OUTGOING_RINGING = 1, // we sent INVITE, waiting for 180/200
        INCOMING_RINGING = 2, // we got INVITE, sent 180, waiting for answer
        ESTABLISHED = 3,      // 200 OK + ACK exchanged; media may flow
    };
    struct CallDialog {
        CallState state = CallState::IDLE;
        std::string peer;
        app::MediaKind kind = app::MediaKind::VOICE;
        uint32_t call_id = 0;
        uint32_t conf_id = 0;          // M18: != 0 -> conference dialog
        bool got_180 = false;      // caller: 180 seen (vs. unreachable)
        bool established_log_done = false; // fire SIP_CALL_ESTABLISHED once
        TimerId first_response_timer = 0;  // caller: no 180 -> unreachable
        TimerId noanswer_timer = 0;        // caller: ringing too long -> CANCEL
        TimerId autoanswer_timer = 0;      // callee: auto-answer countdown
    };
    std::deque<CallDialog> dialogs_;
    uint32_t next_call_id_ = 1;
    uint32_t next_conf_seq_ = 1; // M18: conf_id = (c_rnti << 16) | seq
    uint32_t autoanswer_ms_ = 4000;
    std::string last_call_fail_; // last SIP_CALL_FAILED reason (tests)

    // M17: QoS dedicated bearers (see core/qos.h). The default QCI9 bearer
    // serves msg + the legacy loopback; sig/voice/video are dedicated.
    BearerSet bearers_;

    CallDialog* find_dialog(const std::string& peer, uint32_t call_id);
    CallDialog* find_dialog_by_kind(app::MediaKind kind);
    CallDialog* find_media_dialog(const std::string& peer,
                                  app::MediaKind kind); // ESTABLISHED only
    CallDialog* find_conf_dialog(uint32_t conf_id);     // M18, ESTABLISHED only
    CallDialog* find_incoming_ringing();
    OutStream* find_out_stream(const std::string& peer, app::MediaKind kind);
    OutStream* find_conf_out_stream(uint32_t conf_id);
    InStream* find_in_stream(const std::string& peer, app::MediaKind kind,
                             uint32_t conf_id = 0);
    bool has_conf_dialog(uint32_t conf_id) const; // any state
    void end_dialog(CallDialog& d);           // state-aware hangup
    void erase_dialog(CallDialog& d);         // dialog + its streams, no sig
    void release_bearer_if_unused(Qci qci);   // teardown event when last user goes
    void ensure_conf_media(CallDialog& d);    // M18: conf out-stream (once)

    void send_sig(CallDialog& d, app::SigMethod method);
    void send_sig_to(app::SigMethod method, const std::string& dst,
                     uint32_t call_id, app::MediaKind media,
                     uint32_t conf_id = 0); // off-dialog (486, stray BYE-ack)
    void handle_sig(const std::string& src, const app::SigMessage& msg);
    void begin_media(CallDialog& d);          // caller: 200 OK received
    void fail_call(CallDialog& d, const char* reason);
    void cancel_dialog_timers(CallDialog& d);
    void ensure_bearer(Qci qci);              // QOS_BEARER_SETUP on first use

    // ---- M19: link metrics / CQI / TX power --------------------------------
    void service_link_metrics(); // 1 s: CQI CE + open-loop power + telemetry
    float dl_snr_ewma_ = -100.f;
    float rsrp_ewma_ = -120.f;
    int last_cqi_ = -1;
    double tpc_accum_db_ = 0;
    double tx_power_db_ = 0;
    uint32_t last_pwr_log_ms_ = 0;
    TimerId link_timer_ = 0;

    void send_u2u(const app::U2uPacket& pkt);  // routed to its QCI bearer
    void send_media_tick(OutStream& s);        // one synthetic media packet
    void handle_u2u(const app::U2uPacket& pkt); // RX dispatch
    void enqueue_app_pdu(Qci qci, uint8_t lcid, const std::vector<uint8_t>& pdu);
    void pump_app_bearers();                   // priority drain into HARQ
    void service_am_rx(Qci qci);               // reorder-timeout resync delivery
    void service_um_rx(Qci qci);               // UM reorder timer + delivery
    void ensure_stream_timers();
    void maybe_stop_stream_timers();
    void sweep_stream_acks();
    void emit_stream_stats();
    void stop_streams(bool notify);            // detach/abort cleanup

    uint32_t msg_rx_count_ = 0;
    std::string last_msg_src_;
    std::string last_msg_text_;

    // Build a DATA air frame carrying one (lcid, sdu) and emit it.
    void uplink_send(uint8_t lcid, const std::vector<uint8_t>& sdu);
    void send_frame(AirFrameType type, uint16_t rnti,
                    const std::vector<uint8_t>& payload);

    // Run the RACH+RRC+NAS sequence when both the user asked for it and the
    // prerequisites (system info, idle state) are met.
    void maybe_start_attach();
    void abort_attach(const char* reason);

    // M20: RRC_INACTIVE helpers.
    void maybe_wake_for_activity(); // outbound activity while inactive
    void select_serving_cell();     // M22: camp on strongest audible cell
    uint16_t suspended_crnti_ = 0;
    bool detach_after_resume_ = false;
    bool resume_guard_running_ = false;

    // ---- M14: mobility -------------------------------------------------------
    void handle_ccch_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu);
    void start_measurements();
    void stop_measurements();
    void send_measurement_report();
    void apply_handover(uint16_t target_cell, uint16_t new_crnti);
    void declare_rlf();               // radio link failure -> re-establishment
    void reestablish_failed(const char* reason); // full fallback to attach

    void schedule_rach_window_timer();
    void schedule_backoff_then_retry();
    void schedule_attach_retry();
    void sweep_lost_pings();
    void pump_harq();            // timeouts -> retransmissions
    void send_ack(uint16_t to, const HarqRx::Result& res);

    UeNodeConfig config_;
    HarqTx harq_tx_;
    HarqRx harq_rx_;
    mac::RachUe rach_ue_;
    rrc::RrcUe rrc_ue_;
    nas::NasUe nas_ue_;
    // M13/M17: the user plane runs RLC AM per QoS bearer; the entities live
    // in bearers_ (the QCI9 default bearer serves the legacy loopback).
    TimerList timers_;

    AirBitsSend air_send_;
    uint32_t now_ms_ = 0;
    uint16_t crnti_cache_ = 0;

    bool mib_ok_ = false;
    bool sib1_ok_ = false;
    bool attach_requested_ = false;
    bool attach_guard_running_ = false;
    bool rach_window_timer_running_ = false;
    TimerId rach_window_timer_ = 0;

    std::vector<uint8_t> pending_ccch_; // encoded RRC SetupRequest for MSG3

    // ---- M14: mobility state -------------------------------------------------
    struct NeighborCell {
        uint32_t rx_count = 0;      // SIB receptions (strength proxy)
        uint32_t last_seen_ms = 0;
    };
    std::unordered_map<uint16_t, NeighborCell> cells_; // by cell id
    uint16_t serving_cell_ = 0;       // cell we attached through
    uint32_t last_dl_ms_ = 0;         // RLF watchdog input
    bool reestablish_pending_ = false;
    uint16_t pre_rlf_crnti_ = 0;
    std::vector<uint8_t> reest_req_pdu_;
    TimerId meas_timer_ = 0;
    TimerId reest_guard_ = 0;

    uint32_t app_tx_seq_ = 0;
    uint32_t app_rx_count_ = 0;
    uint32_t app_loss_count_ = 0;
    int64_t last_app_rtt_ms_ = -1;
    uint32_t rtt_samples_ = 0;
    int64_t rtt_min_ms_ = INT64_MAX;
    int64_t rtt_max_ms_ = 0;
    int64_t rtt_sum_ms_ = 0;
    std::unordered_map<uint32_t, uint32_t> app_tx_time_; // seq -> tx ms
    // M12 user-plane confidentiality (keyed after authenticated attach)
    std::array<uint8_t, crypto::kKey256Size> up_key_{};
    bool up_sec_on_ = false;
    uint64_t pdcp_seq_ = 0;
    TimerId traffic_timer_ = 0;
    TimerId loss_sweep_timer_ = 0;
    TimerId stats_timer_ = 0;

    std::mt19937 rng_;
};

}

#endif

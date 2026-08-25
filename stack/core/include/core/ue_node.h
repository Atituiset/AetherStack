#ifndef AETHER_CORE_UE_NODE_H
#define AETHER_CORE_UE_NODE_H

#include "core/radio_frames.h"
#include "common/crypto.h"
#include "core/harq.h"
#include "core/timer_list.h"
#include "mac/rach_ue.h"
#include "nas/nas_ue.h"
#include "rlc/rlc_am.h"
#include "rrc/rrc_ue.h"
#include <cstdint>
#include <functional>
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

    // Advance the timer table to now_ms (monotonic ms since process start).
    void tick(uint32_t now_ms);

    // Commands (wired to the control channel in the process main).
    void attach();  // SIB-gated: starts RACH + RRC once system info is known
    void detach();  // NAS detach + RRC release
    void send_app_data(const std::vector<uint8_t>& payload);

    // Continuous user-plane loopback (M7.1): sends one small packet every
    // traffic_interval_ms while registered. Stopped by stop_traffic(),
    // detach() and attach aborts.
    void start_traffic(uint32_t interval_ms = 100);
    void stop_traffic();
    bool traffic_running() const { return traffic_timer_ != 0; }

    // Observability
    mac::RachState mac_state() const { return rach_ue_.state(); }
    rrc::UeState rrc_state() const { return rrc_ue_.state(); }
    nas::UeState nas_state() const { return nas_ue_.state(); }
    nas::NasUe& nas() { return nas_ue_; }   // test/USIM provisioning access
    uint16_t crnti() const { return crnti_cache_; }
    bool registered() const { return nas_ue_.state() == nas::UeState::REGISTERED; }
    bool has_system_info() const { return mib_ok_ && sib1_ok_; }
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
    void handle_sysinfo_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu);
    void handle_dedicated_sdu(uint8_t lcid, const std::vector<uint8_t>& sdu);
    void handle_pong(const std::vector<uint8_t>& data); // RTT accounting

    // Build a DATA air frame carrying one (lcid, sdu) and emit it.
    void uplink_send(uint8_t lcid, const std::vector<uint8_t>& sdu);
    void send_frame(AirFrameType type, uint16_t rnti,
                    const std::vector<uint8_t>& payload);

    // Run the RACH+RRC+NAS sequence when both the user asked for it and the
    // prerequisites (system info, idle state) are met.
    void maybe_start_attach();
    void abort_attach(const char* reason);

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
    // M13: the user-plane bearer runs in RLC AM (ARQ). app_am_tx_ feeds the
    // uplink; app_am_rx_dl_ reassembles downlink PDUs (echo path).
    rlc::AmTx app_am_tx_;
    rlc::AmRx app_am_rx_dl_;
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

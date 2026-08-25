#ifndef AETHER_CORE_BS_NODE_H
#define AETHER_CORE_BS_NODE_H

#include "core/harq.h"
#include "core/radio_frames.h"
#include "core/timer_list.h"
#include "mac/rach_bs.h"
#include "nas/nas_bs.h"
#include "rrc/rrc_bs.h"
#include <cstdint>
#include <functional>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace core {

struct BsNodeConfig {
    uint32_t sib_period_ms = 200;
    size_t max_dl_queue_per_ue = 64; // scheduler backpressure bound
};

// BS-side protocol stack orchestration. Mirrors UeNode: owns the BS layer
// entities, periodically broadcasts MIB/SIB1, terminates RACH/RRC/NAS and
// loops user-plane data back to the UE (ping-pong).
class BsNode {
public:
    using AirBitsSend = std::function<void(const std::vector<uint8_t>&)>;

    explicit BsNode(const BsNodeConfig& config = BsNodeConfig{});

    void set_air_send(AirBitsSend send);

    // Ingest bits decoded from one received radio burst.
    void on_air_bits(const std::vector<uint8_t>& bits);

    // Advance the timer table to now_ms (monotonic ms since process start).
    void tick(uint32_t now_ms);

    // Start system information broadcast (fires once immediately, then
    // repeats every sib_period_ms while tick() is driven).
    void start_broadcast();

    // Observability
    bool ue_connected(uint16_t crnti) const { return rrc_bs_.is_ue_connected(crnti); }
    size_t registered_ue_count() const { return tmsi_to_crnti_.size(); }
    size_t active_flow_count() const { return flows_.size(); }

private:
    // M11: per-UE downlink flow — its own HARQ entities and a queue the
    // round-robin scheduler drains. Uplink uses configured grants (each UE
    // may transmit freely); only the shared downlink needs scheduling.
    struct DlFlow {
        HarqTx harq_tx;
        HarqRx harq_rx;
        std::deque<std::vector<uint8_t>> queue; // coded frames ready to air
        size_t enqueued = 0, dropped = 0;
    };
    DlFlow& flow(uint16_t rnti);

    void broadcast_sib();
    void handle_air_frame(const AirFrame& frame);
    void handle_dl_data(uint16_t rnti, const std::vector<uint8_t>& pdu);
    void downlink_send(uint16_t rnti, uint8_t lcid, const std::vector<uint8_t>& sdu);
    void downlink_raw(uint16_t rnti, uint8_t lcid, const std::vector<uint8_t>& sdu);
    void schedule_downlink();   // round-robin: one new block per flow/tick
    void pump_flows();          // per-flow HARQ timeout retransmissions
    void send_ack(uint16_t to, const HarqRx::Result& res);
    void send_frame(AirFrameType type, uint16_t rnti,
                    const std::vector<uint8_t>& payload);

    BsNodeConfig config_;
    mac::RachBs rach_bs_;
    rrc::RrcBs rrc_bs_;
    nas::NasBs nas_bs_;
    TimerList timers_;

    AirBitsSend air_send_;
    uint32_t now_ms_ = 0;
    uint16_t ul_crnti_ = 0; // C-RNTI of the UL frame currently being processed
    std::unordered_map<uint32_t, uint16_t> tmsi_to_crnti_;
    std::unordered_map<uint16_t, DlFlow> flows_; // keyed by C-RNTI
};

}

#endif

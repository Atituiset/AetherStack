#ifndef AETHER_CORE_BS_NODE_H
#define AETHER_CORE_BS_NODE_H

#include "common/crypto.h"
#include "core/harq.h"
#include "core/radio_frames.h"
#include "core/timer_list.h"
#include "mac/rach_bs.h"
#include "nas/nas_bs.h"
#include "rlc/rlc_am.h"
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
    // M14: cell identity broadcast in SIB1; drives HO decisions.
    uint16_t pci = 0;
    uint16_t cell_id = 1;
    std::string plmn_id = "46001";
    uint16_t tac = 1;
    bool auto_handover = true; // react to MEAS_REPORT neighbours automatically
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

    // M14: switch the beacon off/on (cell shutdown / recovery scenarios).
    // Unicast scheduling continues regardless.
    void set_sib_enabled(bool on) { sib_enabled_ = on; }

    // Observability
    bool ue_connected(uint16_t crnti) const { return rrc_bs_.is_ue_connected(crnti); }
    size_t registered_ue_count() const { return tmsi_to_crnti_.size(); }
    size_t active_flow_count() const { return flows_.size(); }
    nas::NasBs& nas() { return nas_bs_; }   // test/provisioning access
    uint16_t cell_id() const { return config_.cell_id; }

    // ---- M14: mobility -------------------------------------------------------
    // Session context carried across an X2-like handover preparation.
    struct HoContext {
        uint32_t tmsi = 0;
        std::string imsi;
        std::array<uint8_t, crypto::kKey256Size> up_key{};
        bool sec_on = false;
    };
    // Returns the new C-RNTI allocated by `target_cell_id`, or nullopt when
    // that cell refuses/cannot prepare. Wired by the owner (test, process
    // main or a real X2 link).
    using HoCoordinator =
        std::function<std::optional<uint16_t>(uint16_t target_cell_id,
                                              const HoContext& ctx)>;

    void set_ho_coordinator(HoCoordinator fn) { ho_coordinator_ = std::move(fn); }

    // Source side: migrate `crnti` to `target_cell_id` (manual entry point;
    // the automatic policy reacts to MEAS_REPORTs).
    void request_handover(uint16_t crnti, uint16_t target_cell_id);

    // Target side: reserve a connected context + security state. The UE
    // confirms later with HO_COMPLETE.
    std::optional<uint16_t> prepare_handover(const HoContext& ctx);

    // M14: paging — deliver `imsi` in the next SIB broadcast; idle UEs auto-
    // respond with a service request (fresh attach).
    void page(const std::string& imsi) { paging_target_ = imsi; }

private:
    // M11: per-UE downlink flow — its own HARQ entities and a queue the
    // round-robin scheduler drains. Uplink uses configured grants (each UE
    // may transmit freely); only the shared downlink needs scheduling.
    struct DlFlow {
        HarqTx harq_tx;
        HarqRx harq_rx;
        std::deque<std::vector<uint8_t>> queue; // coded frames ready to air
        size_t enqueued = 0, dropped = 0;
        // M12: user-plane confidentiality keyed after authenticated attach
        std::array<uint8_t, crypto::kKey256Size> up_key{};
        bool sec_on = false;
        uint64_t dl_seq = 0;
        // M13: user-plane bearer in RLC AM. dl_am_tx feeds the downlink
        // (echo path); ul_am_rx reassembles what the UE sends.
        rlc::AmTx dl_am_tx;
        rlc::AmRx ul_am_rx;
    };
    DlFlow& flow(uint16_t rnti);

    void broadcast_sib();
    bool sib_enabled_ = true;
    bool broadcasting_ = false;
    void handle_air_frame(const AirFrame& frame);
    void handle_dl_data(uint16_t rnti, const std::vector<uint8_t>& pdu);
    void handle_ccch_sdu(uint16_t rnti, const std::vector<uint8_t>& sdu); // M14
    uint32_t tmsi_for_crnti(uint16_t crnti) const;                       // M14
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

    // M14 mobility state
    HoCoordinator ho_coordinator_;
    std::string paging_target_;                 // one-shot SIB paging record
    struct InitiatedHo { uint16_t target_cell; uint16_t new_crnti; };
    std::unordered_map<uint16_t, InitiatedHo> initiated_ho_;   // source side
    std::unordered_map<uint16_t, HoContext> ho_prepared_;      // target side
    uint16_t next_ho_crnti_ = 0x1001;           // HO allocations avoid RACH range

    AirBitsSend air_send_;
    uint32_t now_ms_ = 0;
    uint16_t ul_crnti_ = 0; // C-RNTI of the UL frame currently being processed
    std::unordered_map<uint32_t, uint16_t> tmsi_to_crnti_;
    std::unordered_map<uint16_t, DlFlow> flows_; // keyed by C-RNTI
};

}

#endif

#ifndef AETHER_CORE_BS_NODE_H
#define AETHER_CORE_BS_NODE_H

#include "cn/cn_link.h"
#include "app/u2u.h"
#include "common/crypto.h"
#include "core/harq.h"
#include "core/qos.h"
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
    // M22: C-RNTI allocation base for this cell (RACH from base, HO from
    // base+0x1000) — cells on the shared medium must not collide.
    uint16_t crnti_base = 0x0001;
    // M20: suspend a connected UE to RRC_INACTIVE after this much user-plane
    // inactivity (0 = never). Any non-ACK uplink refreshes the timer, so an
    // active call/media stream is never suspended.
    uint32_t inactive_ms = 0;
};

// BS-side protocol stack orchestration. Mirrors UeNode: owns the BS layer
// entities, periodically broadcasts MIB/SIB1, terminates RACH/RRC/NAS and
// loops user-plane data back to the UE (ping-pong).
class BsNode {
public:
    using AirBitsSend = std::function<void(const std::vector<uint8_t>&)>;

    explicit BsNode(const BsNodeConfig& config = BsNodeConfig{});

    void set_air_send(AirBitsSend send);

    // M19: extended radio callback carrying the per-burst MCS the receiver
    // shell should modulate with (per-flow link adaptation). Falls back to
    // the plain callback when unset (tests).
    using AirBitsSendEx = std::function<void(const AirFrame&, int mcs,
                                             const std::vector<uint8_t>&)>;
    void set_air_send_ex(AirBitsSendEx send) { air_send_ex_ = std::move(send); }

    // Ingest bits decoded from one received radio burst.
    void on_air_bits(const std::vector<uint8_t>& bits);

    // M19: same ingest plus per-burst radio metrics (UL DMRS SNR estimate +
    // burst power), consumed by the next frame's flow for TPC.
    void on_air_bits_with_metrics(const std::vector<uint8_t>& bits,
                                  float snr_db, float pwr_dbm);

    // M19 link-adaptation observability (tests/status).
    int dl_mcs(uint16_t rnti) const;
    int flow_cqi(uint16_t rnti) const;

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
    // M17: per-flow QoS bearer observability (tests/status).
    bool flow_bearer_established(uint16_t rnti, int qci) const;
    // M18: conference bridge observability (tests).
    size_t conf_member_count(uint32_t conf_id) const;
    size_t conf_count() const { return confs_.size(); }
    // M20: RRC_INACTIVE observability (tests/status).
    bool flow_suspended(uint16_t rnti) const;
    nas::NasBs& nas() { return nas_bs_; }   // test/provisioning access
    rrc::RrcBs& rrc() { return rrc_bs_; }   // test access (context drops)
    uint16_t cell_id() const { return config_.cell_id; }

    // ---- M14: mobility -------------------------------------------------------
    // Session context carried across an X2-like handover preparation.
    struct HoContext {
        uint32_t tmsi = 0;
        std::string imsi;
        std::array<uint8_t, crypto::kKey256Size> up_key{};
        bool sec_on = false;
        uint16_t from_cell = 0; // M22: source cell id (for HANDOVER_DONE)
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

    // ---- M15: core-network separation ----------------------------------------
    // Wire the gNB to external AMF/UPF entities. When both links are set the
    // embedded NasBs and the local user-plane echo are bypassed: NAS PDUs
    // tunnel opaquely over UPLINK/DOWNLINK_NAS, and user-plane SDUs go to
    // the UPF anchor (which routes downlink back). Leave unset for the
    // legacy single-node behaviour (used by most tests).
    struct CnEndpoints {
        cn::CnLink* amf = nullptr;
        cn::CnLink* upf = nullptr;
        uint16_t gnb_cell = 1; // announced in NG_SETUP
    };
    // Must be called before any UE attaches. Performs NG_SETUP and installs
    // the message handlers.
    void attach_core(CnEndpoints ep);

    bool core_separated() const { return cn_amf_ != nullptr; }

    // M22: wire an Xn link to the peer gNB (dual-BS mobility). When set
    // (and no ho_coordinator_/separated core), handovers are prepared over
    // Xn and U2U downlink for UEs that moved to the peer is forwarded.
    void attach_xn(cn::CnLink* link, uint16_t peer_cell);

private:
    // M11: per-UE downlink flow — its own HARQ entities and QoS bearers the
    // scheduler drains by strict priority (M17). Uplink uses configured
    // grants (each UE may transmit freely); only the shared downlink needs
    // scheduling.
    struct DlFlow {
        HarqTx harq_tx;
        HarqRx harq_rx;
        // M17: per-QCI bearers (each with RLC AM + queue) plus a control
        // queue for CCCH/NAS/RLC STATUS. See core/qos.h.
        BearerSet bearers;
        size_t enqueued = 0, dropped = 0;
        // M12: user-plane confidentiality keyed after authenticated attach
        std::array<uint8_t, crypto::kKey256Size> up_key{};
        bool sec_on = false;
        // M17: PDCP COUNT shared across bearers (simplification; the nonce
        // stays unique — see docs/m17_plan.md).
        uint64_t dl_seq = 0;
        // M19: link adaptation + UL power-control state.
        int cqi = -1;               // last CQI report (1..15), -1 none
        int last_cqi_logged = -1;   // CQI_REPORT event throttle (change-only)
        int mcs = 0;                // DL MCS index (phy::Mcs) from CQI
        float ul_snr_ewma = -100.f; // UL DMRS SNR, drives TPC
        uint32_t last_tpc_ms = 0;
        // M20: RRC_INACTIVE — context kept, data path parked. DL SDUs still
        // queue (delivered after resume); the UE is paged on first arrival.
        bool suspended = false;
        uint32_t last_activity_ms = 0;
    };
    DlFlow& flow(uint16_t rnti);

    void broadcast_sib();
    bool sib_enabled_ = true;
    bool broadcasting_ = false;
    void handle_air_frame(const AirFrame& frame);
    void handle_dl_data(uint16_t rnti, const std::vector<uint8_t>& pdu);
    void handle_ul_app_sdu(uint16_t rnti, const std::vector<uint8_t>& data);
    void handle_ccch_sdu(uint16_t rnti, const std::vector<uint8_t>& sdu); // M14
    uint32_t tmsi_for_crnti(uint16_t crnti) const;                       // M14
    // M16: UE-to-UE forwarding. Resolve a destination IMSI to the C-RNTI of
    // a registered, attached UE (0 when unknown); APP_FORWARD is aggregated
    // per (src,dst,kind) over a 1 s window to keep voice/video rates sane.
    uint16_t crnti_for_imsi(const std::string& imsi) const;
    void log_forward(const app::U2uPacket& pkt, size_t bytes);
    void flush_forward_log();
    // M17: QoS bearer setup/teardown events for a flow.
    void ensure_bs_bearer(uint16_t rnti, DlFlow& f, Qci qci);
    void log_bs_bearer_teardowns(uint16_t rnti, DlFlow& f);
    // ---- M18: 3-party conference (audio bridge) -------------------------------
    // The BS splices conference membership together by snooping the SIP-lite
    // signaling it forwards (every conf dialog message carries the conf_id),
    // then fans each participant's voice media out to all the others.
    struct Conference {
        std::string host;
        std::vector<std::string> members;  // joined, host included
        std::vector<std::string> invited;  // every party the host INVITEd
        std::vector<std::string> resolved; // invited parties with a final answer
        bool ever_multi = false;           // membership reached >= 2
    };
    void snoop_conf_sig(const std::string& src, const std::string& dst,
                        const app::SigMessage& msg);
    void conf_leave(uint32_t conf_id, const std::string& imsi,
                    const char* reason);
    void conf_maybe_end(uint32_t conf_id);
    void purge_conf_member(const std::string& imsi); // detach / flow erase
    void bridge_conf_media(uint16_t src_rnti, const app::U2uPacket& pkt);
    std::unordered_map<uint32_t, Conference> confs_;
    struct FwdAgg {
        std::string src, dst, kind;
        uint32_t count = 0;
        uint64_t bytes = 0;
        uint32_t window_start_ms = 0;
    };
    std::vector<FwdAgg> fwd_agg_; // one entry per active (src,dst,kind) tuple
    // M22: Xn interface to the peer gNB.
    void handle_xn_message(const cn::CnMessage& msg);
    void release_ho_source(uint16_t old_rnti); // context teardown post-HO
    void xn_forward_data(const std::string& dst_imsi,
                         const std::vector<uint8_t>& sdu);
    bool forward_u2u_dl(const app::U2uPacket& pkt,
                        const std::vector<uint8_t>& data,
                        uint16_t exclude_rnti = 0);
    cn::CnLink* xn_ = nullptr;
    uint16_t xn_peer_cell_ = 0;
    struct PendingXnHo {
        uint16_t source_crnti = 0;
        uint16_t target_cell = 0;
    };
    std::unordered_map<uint32_t, PendingXnHo> pending_xn_ho_; // by tmsi

    // M15: NG/GTP-like core-network plumbing.
    void cn_send_nas(uint32_t tmsi, uint16_t rnti,
                     const std::vector<uint8_t>& nas_pdu);
    void cn_send_uplink_data(uint32_t tmsi, uint16_t rnti,
                             const std::vector<uint8_t>& data);
    void handle_cn_message(const cn::CnMessage& msg);
    void deliver_dl_nas(uint32_t tmsi, uint16_t rnti,
                        const std::vector<uint8_t>& nas_pdu);
    void downlink_send(uint16_t rnti, uint8_t lcid, const std::vector<uint8_t>& sdu);
    void downlink_raw(uint16_t rnti, uint8_t lcid, const std::vector<uint8_t>& sdu);
    void schedule_downlink();   // per-flow priority drain of the QoS bearers
    void pump_flows();          // per-flow HARQ timeout retransmissions
    void sweep_tpc();           // M19: closed-loop UL power control
    void sweep_inactive();      // M20: RRC_INACTIVE suspend on idle UEs
    int mcs_for_frame(const AirFrame& frame); // M19 per-burst MCS selection
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

    // M15: core-network separation state
    cn::CnLink* cn_amf_ = nullptr;
    cn::CnLink* cn_upf_ = nullptr;
    bool ng_setup_ok_ = false;

    AirBitsSend air_send_;
    AirBitsSendEx air_send_ex_; // M19: MCS-carrying radio callback
    float pending_ul_snr_ = -100.f; // metrics of the burst being ingested
    uint32_t now_ms_ = 0;
    uint16_t ul_crnti_ = 0; // C-RNTI of the UL frame currently being processed
    std::unordered_map<uint32_t, uint16_t> tmsi_to_crnti_;
    std::unordered_map<uint16_t, DlFlow> flows_; // keyed by C-RNTI
    // M22: flow erases requested from reentrant contexts (Xn handlers run
    // synchronously inside schedule_downlink's flows_ iteration in
    // in-process tests) are deferred to the next tick.
    std::vector<uint16_t> pending_flow_erase_;
};

}

#endif

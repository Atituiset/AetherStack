#ifndef AETHER_RRC_RRC_BS_H
#define AETHER_RRC_RRC_BS_H

#include "rrc/rrc_types.h"
#include "rrc/rrc_messages.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rrc {

class RrcBs {
public:
    RrcBs() = default;

    using SendCallback = std::function<void(uint16_t rnti, const std::vector<uint8_t>&)>;
    // M14: fired when a re-establishment migrates a context to a new C-RNTI
    // so the owning node can remap its flows.
    using ReestCallback = std::function<void(uint16_t old_rnti, uint16_t new_rnti)>;

    void set_send_callback(SendCallback cb);
    void set_reest_callback(ReestCallback cb);

    // M22: C-RNTI allocation base + resume-identity ownership range
    // ([base, base+0x2000): RACH bases from base, HO from base+0x1000).
    // Cells on the shared medium must not collide.
    void set_crnti_base(uint16_t base) {
        next_crnti_ = base;
        crnti_base_ = base;
    }

    // M14: cell identity used in SIB1 broadcasts and HO decisions.
    void set_cell_identity(uint16_t cell_id, std::string plmn_id,
                           uint16_t tac = 1) {
        sib1_.cell_id = cell_id;
        sib1_.plmn_id = std::move(plmn_id);
        sib1_.tac = tac;
    }
    uint16_t cell_id() const { return sib1_.cell_id; }

    void handle_message(uint16_t rnti, const std::vector<uint8_t>& pdu);
    bool is_ue_connected(uint16_t rnti) const;

    Mib broadcast_mib() const;
    Sib1 broadcast_sib1() const { return sib1_; }

    struct UeContext {
        uint16_t c_rnti = 0;
        UeState state = UeState::IDLE;
        uint32_t resume_id = 0; // M20: valid while INACTIVE
    };

    const UeContext* find_ue(uint16_t rnti) const;

    // M14: handover target side — create a CONNECTED context out of band
    // (the UE will confirm later with HO_COMPLETE). The caller owns C-RNTI
    // allocation for handovers.
    void admit_connected(uint16_t crnti);

    // M14: drop the context entirely (source side of a completed handover).
    void release_context(uint16_t rnti);

    // M20: suspend a connected UE to RRC_INACTIVE (inactivity timer or the
    // UE's own suspend request): allocates the resume identity and sends
    // the suspend RELEASE. Registration/context are kept.
    void suspend_context(uint16_t rnti);
    // M20: recovery for a lost suspend RELEASE: the UE is provably alive
    // (uplink arrived) — flip its context back to CONNECTED.
    void reactivate_context(uint16_t rnti);

    // M20: fired when a UE enters INACTIVE so the owning node can park the
    // data path (HARQ reset, DL queue gating).
    using SuspendCallback = std::function<void(uint16_t rnti)>;
    void set_suspend_callback(SuspendCallback cb) { suspend_cb_ = std::move(cb); }

private:
    SendCallback send_cb_;
    ReestCallback reest_cb_;
    SuspendCallback suspend_cb_;
    Sib1 sib1_; // identity carrier (plmn/tac/cell_id)
    std::unordered_map<uint16_t, UeContext> ue_contexts_;
    uint16_t next_crnti_ = 0x0001;
    uint16_t crnti_base_ = 0x0001; // M22: resume-id ownership range
    uint32_t next_resume_seq_ = 1;
};

}

#endif

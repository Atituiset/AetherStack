#ifndef AETHER_RRC_RRC_UE_H
#define AETHER_RRC_RRC_UE_H

#include "rrc/rrc_types.h"
#include "rrc/rrc_messages.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace rrc {

class RrcUe {
public:
    RrcUe() = default;

    using SendCallback = std::function<void(const std::vector<uint8_t>&)>;

    void set_send_callback(SendCallback cb);

    UeState state() const { return state_; }

    void on_mib_received(const Mib& mib);
    void on_sib1_received(const Sib1& sib1);

    // cell_id: M22 multi-cell deployments name the cell the SetupRequest
    // targets (0 = legacy single-cell, any BS may answer).
    void start_connection(uint16_t cell_id = 0);
    void release(); // UE-initiated release (sends RELEASE, drops to IDLE)
    // M20: ask the network to suspend us (RELEASE with suspend request);
    // the BS answers with a suspend RELEASE carrying the resume identity.
    void request_suspend();
    // M20: begin a resume from INACTIVE (the node drives RACH with the
    // RESUME_REQUEST PDU); transitions to CONNECTING.
    void start_resume();
    // Abort any in-flight procedure and drop to IDLE without transmitting
    // (fault recovery, e.g. attach guard timeout).
    void force_idle();
    // M14: adopt an existing connection out of band (handover command or
    // successful re-establishment) — no signalling on this entity.
    void restore_connected(uint16_t crnti);
    void on_message(const std::vector<uint8_t>& pdu);

    uint16_t assigned_crnti() const { return assigned_crnti_; }

    // M20: RRC_INACTIVE — suspend arrived (context + registration kept).
    bool inactive() const { return state_ == UeState::INACTIVE; }
    uint32_t resume_id() const { return resume_id_; }
    uint16_t suspended_crnti() const { return suspended_crnti_; }
    // M20: fired when the network suspends us / answers a resume, so the
    // owning node can gate/restore its data path.
    using SuspendCallback = std::function<void(uint32_t resume_id)>;
    using ResumeCallback = std::function<void(bool ok, uint16_t new_crnti)>;
    void set_suspend_callback(SuspendCallback cb) { suspend_cb_ = std::move(cb); }
    void set_resume_callback(ResumeCallback cb) { resume_cb_ = std::move(cb); }

private:
    void transition(UeState new_state);

    UeState state_ = UeState::IDLE;
    uint16_t assigned_crnti_ = 0;
    Mib received_mib_;
    Sib1 received_sib1_;
    SendCallback send_cb_;
    // M20: suspend/resume bookkeeping.
    uint32_t resume_id_ = 0;
    uint16_t suspended_crnti_ = 0;
    SuspendCallback suspend_cb_;
    ResumeCallback resume_cb_;
};

}

#endif

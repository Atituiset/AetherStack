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

    void start_connection();
    void release(); // UE-initiated release (sends RELEASE, drops to IDLE)
    // Abort any in-flight procedure and drop to IDLE without transmitting
    // (fault recovery, e.g. attach guard timeout).
    void force_idle();
    // M14: adopt an existing connection out of band (handover command or
    // successful re-establishment) — no signalling on this entity.
    void restore_connected(uint16_t crnti);
    void on_message(const std::vector<uint8_t>& pdu);

    uint16_t assigned_crnti() const { return assigned_crnti_; }

private:
    void transition(UeState new_state);

    UeState state_ = UeState::IDLE;
    uint16_t assigned_crnti_ = 0;
    Mib received_mib_;
    Sib1 received_sib1_;
    SendCallback send_cb_;
};

}

#endif

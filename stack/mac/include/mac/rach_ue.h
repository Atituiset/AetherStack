#ifndef AETHER_MAC_RACH_UE_H
#define AETHER_MAC_RACH_UE_H

#include "mac/rach_common.h"
#include <cstdint>
#include <vector>

namespace mac {

class RachUe {
public:
    explicit RachUe(const RachConfig& config = {});

    void set_send_callback(RachSendCallback cb);
    void set_state_callback(RachStateCallback cb);

    RachState state() const { return state_; }

    // Trigger RACH procedure (e.g., from RRC)
    void start_rach();

    // Handle received MSG2 (RAR)
    void on_rar_received(RaRnti ra_rnti, uint16_t timing_advance, uint8_t ul_grant);

    // Handle received MSG4 (contention resolution)
    void on_contention_resolve(uint16_t crnti);

    // Timeout handlers
    void on_rar_timeout();
    void on_contention_resolve_timeout();

private:
    void transition(RachState new_state);

    RachConfig config_;
    RachState state_ = RachState::IDLE;
    RaRnti assigned_ra_rnti_ = 0;
    uint16_t assigned_crnti_ = 0;
    uint8_t preamble_tx_count_ = 0;
    RachSendCallback send_cb_;
    RachStateCallback state_cb_;
};

}

#endif

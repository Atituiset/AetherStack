#ifndef AETHER_MAC_RACH_UE_H
#define AETHER_MAC_RACH_UE_H

#include "mac/rach_common.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace mac {

class RachUe {
public:
    explicit RachUe(const RachConfig& config = {});

    // Optional provider for the real CCCH payload (e.g. encoded RRC
    // SetupRequest) carried inside MSG3 after its 3-byte header.
    using Msg3Provider = std::function<std::vector<uint8_t>()>;

    void set_send_callback(RachSendCallback cb);
    void set_state_callback(RachStateCallback cb);
    void set_msg3_provider(Msg3Provider provider);

    RachState state() const { return state_; }

    // Trigger RACH procedure (e.g., from RRC)
    void start_rach();
    // M22: re-target the next RACH at a (possibly different) cell —
    // preamble partitioning by cell id (see rach_common.h).
    void set_preamble_index(PreambleIndex idx) { config_.preamble_index = idx; }

    // Handle received MSG2 (RAR)
    void on_rar_received(RaRnti ra_rnti, uint16_t timing_advance, uint8_t ul_grant);

    // Handle received MSG4 (contention resolution). ra_rnti identifies the
    // random-access context the C-RNTI was granted on; mismatches (another
    // UE's MSG4 on the shared medium) are ignored.
    void on_contention_resolve(uint16_t crnti, RaRnti ra_rnti);

    // Timeout handlers
    void on_rar_timeout();
    void on_contention_resolve_timeout();

    // Abort any in-flight procedure and drop to IDLE (fault recovery).
    void force_idle();

private:
    void transition(RachState new_state);

    RachConfig config_;
    RachState state_ = RachState::IDLE;
    RaRnti assigned_ra_rnti_ = 0;
    uint16_t assigned_crnti_ = 0;
    uint8_t preamble_tx_count_ = 0;
    RachSendCallback send_cb_;
    RachStateCallback state_cb_;
    Msg3Provider msg3_provider_;
};

}

#endif

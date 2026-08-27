#ifndef AETHER_MAC_RACH_COMMON_H
#define AETHER_MAC_RACH_COMMON_H

#include <cstdint>
#include <functional>
#include <vector>

namespace mac {

// RACH preamble index (0-63 for 5G NR)
using PreambleIndex = uint8_t;

// RA-RNTI (Random Access Radio Network Temporary Identifier)
using RaRnti = uint16_t;

// M22: multi-cell preamble partitioning — on the shared medium both cells
// hear every MSG1, so a UE targets ONE cell by drawing its preamble from
// that cell's half of the space (cell 1: 0-31, cell 2: 32-63). The BS
// answers only preambles addressed to its own cell id.
inline PreambleIndex preamble_for_cell(PreambleIndex base, uint16_t cell_id) {
    const uint8_t half = cell_id >= 2 ? 1 : 0; // two-cell deployment
    return static_cast<PreambleIndex>(half * 32 + (base & 0x1F));
}
inline uint16_t cell_for_preamble(PreambleIndex p) {
    return static_cast<uint16_t>((p >> 5) + 1);
}

// RA-RNTI derivation shared by BS (RAR/MSG4 addressing) and UE (matching
// only its own RAR/MSG4 on the shared medium).
inline RaRnti ra_rnti_for_preamble(PreambleIndex p) {
    return static_cast<RaRnti>(0x4300 | p);
}

// RACH message types
enum class RachMsgType : uint8_t {
    MSG1_PRACH = 1,
    MSG2_RAR = 2,
    MSG3_RRC_REQ = 3,
    MSG4_CONTENTION_RESOLVE = 4,
};

// RACH state (UE side)
enum class RachState : uint8_t {
    IDLE = 0,
    WAIT_RAR = 1,
    WAIT_CONTENTION_RESOLVE = 2,
    CONNECTED = 3,
};

// Callback type: send a RACH message (bytes) to lower layer
using RachSendCallback = std::function<void(RachMsgType, const std::vector<uint8_t>&)>;

// Callback type: notify state change
using RachStateCallback = std::function<void(RachState old_state, RachState new_state)>;

const char* rach_state_str(RachState s);

// RACH configuration
struct RachConfig {
    // M22: default lives in cell 1's half of the partitioned preamble
    // space (0-31); production UEs override per cell via preamble_for_cell.
    PreambleIndex preamble_index = 10;
    uint16_t rar_window_ms = 10;
    uint16_t content_resolve_window_ms = 20;
    uint8_t max_preamble_transmissions = 3;
};

}

#endif

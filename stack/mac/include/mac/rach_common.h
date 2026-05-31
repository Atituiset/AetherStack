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
    PreambleIndex preamble_index = 42;
    uint16_t rar_window_ms = 10;
    uint16_t content_resolve_window_ms = 20;
    uint8_t max_preamble_transmissions = 3;
};

}

#endif

#ifndef AETHER_RRC_RRC_TYPES_H
#define AETHER_RRC_RRC_TYPES_H

#include <cstdint>

namespace rrc {

enum class UeState : uint8_t {
    IDLE = 0,
    CONNECTING = 1,
    CONNECTED = 2,
};

const char* ue_state_str(UeState s);

enum class RrcMessageType : uint8_t {
    SETUP_REQUEST = 1,
    SETUP = 2,
    SETUP_COMPLETE = 3,
    RELEASE = 4,
    MIB_BROADCAST = 10,
    SIB1_BROADCAST = 11,
};

}

#endif

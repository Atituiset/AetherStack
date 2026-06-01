#include "rrc/rrc_types.h"

namespace rrc {

const char* ue_state_str(UeState s) {
    switch (s) {
        case UeState::IDLE: return "IDLE";
        case UeState::CONNECTING: return "CONNECTING";
        case UeState::CONNECTED: return "CONNECTED";
    }
    return "UNKNOWN";
}

}

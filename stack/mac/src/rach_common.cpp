#include "mac/rach_common.h"

namespace mac {

const char* rach_state_str(RachState s) {
    switch (s) {
        case RachState::IDLE: return "IDLE";
        case RachState::WAIT_RAR: return "WAIT_RAR";
        case RachState::WAIT_CONTENTION_RESOLVE: return "WAIT_CR";
        case RachState::CONNECTED: return "CONNECTED";
    }
    return "UNKNOWN";
}

}

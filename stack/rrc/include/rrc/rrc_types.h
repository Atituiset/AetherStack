#ifndef AETHER_RRC_RRC_TYPES_H
#define AETHER_RRC_RRC_TYPES_H

#include <cstdint>

namespace rrc {

enum class UeState : uint8_t {
    IDLE = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    INACTIVE = 3, // M20: RRC_INACTIVE — context kept, registration retained
};

const char* ue_state_str(UeState s);

enum class RrcMessageType : uint8_t {
    SETUP_REQUEST = 1,
    SETUP = 2,
    SETUP_COMPLETE = 3,
    RELEASE = 4,
    MIB_BROADCAST = 10,
    SIB1_BROADCAST = 11,

    // M14 mobility signalling
    MEAS_REPORT = 20,              // UE -> serving: neighbour strengths
    HO_COMMAND = 21,               // source -> UE: [target_cell:2][new_crnti:2]
    HO_COMPLETE = 22,              // UE -> target: [new_crnti:2]
    REESTABLISHMENT_REQUEST = 23,  // UE -> cell: [old_crnti:2][old_cell:2]
    REESTABLISHMENT_OK = 24,       // cell -> UE: [new_crnti:2]
    REESTABLISHMENT_FAILURE = 25,  // cell -> UE: context gone

    // M20: RRC_INACTIVE suspend/resume. RELEASE carries
    // [crnti:2][flags:1][resume_id:4] when flags != 0
    // (bit0 = suspend-with-id BS->UE, bit1 = suspend request UE->BS).
    RESUME_REQUEST = 30,           // UE -> cell: [resume_id:4]
    RESUME_OK = 31,                // cell -> UE: [new_crnti:2]
    RESUME_FAILURE = 32,           // cell -> UE: stale/unknown resume_id
};

}

#endif

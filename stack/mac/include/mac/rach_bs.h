#ifndef AETHER_MAC_RACH_BS_H
#define AETHER_MAC_RACH_BS_H

#include "mac/rach_common.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mac {

class RachBs {
public:
    RachBs() = default;

    void set_send_callback(RachSendCallback cb);
    void set_state_callback(RachStateCallback cb);

    // Handle received MSG1 (PRACH preamble from UE)
    void on_prach_received(PreambleIndex preamble_idx);

    // Handle received MSG3 (RRC Setup Request from UE)
    void on_msg3_received(RaRnti ra_rnti, const std::vector<uint8_t>& msg3_data);

    struct UeContext {
        RaRnti ra_rnti = 0;
        uint16_t c_rnti = 0;
        PreambleIndex preamble = 0;
        bool rach_complete = false;
    };

    const UeContext* find_ue(RaRnti ra_rnti) const;
    bool is_rach_complete(RaRnti ra_rnti) const;

private:
    RachSendCallback send_cb_;
    RachStateCallback state_cb_;
    std::unordered_map<RaRnti, UeContext> ue_contexts_;
    uint16_t next_crnti_ = 0x0001;
};

}

#endif

#ifndef AETHER_MAC_RACH_BS_H
#define AETHER_MAC_RACH_BS_H

#include "mac/rach_common.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace mac {

class RachBs {
public:
    RachBs() = default;

    // Receives the CCCH payload carried in MSG3 (after its 3-byte header)
    // together with the C-RNTI assigned to that UE.
    using CcchHandler = std::function<void(uint16_t crnti, const std::vector<uint8_t>& ccch)>;

    void set_send_callback(RachSendCallback cb);
    void set_state_callback(RachStateCallback cb);
    void set_ccch_handler(CcchHandler handler);

    // M22: this cell's identity — MSG1 preambles addressed to another
    // cell (preamble partitioning) are ignored.
    void set_cell_id(uint16_t cell_id) { cell_id_ = cell_id; }
    // M22: C-RNTI allocation base (cells must not collide on the shared
    // medium). Also seeds the RRC layer's range via BsNode.
    void set_crnti_base(uint16_t base) { next_crnti_ = base; }

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
    CcchHandler ccch_handler_;
    std::unordered_map<RaRnti, UeContext> ue_contexts_;
    uint16_t cell_id_ = 1;      // M22
    uint16_t next_crnti_ = 0x0001;
};

}

#endif

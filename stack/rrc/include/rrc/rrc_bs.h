#ifndef AETHER_RRC_RRC_BS_H
#define AETHER_RRC_RRC_BS_H

#include "rrc/rrc_types.h"
#include "rrc/rrc_messages.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace rrc {

class RrcBs {
public:
    RrcBs() = default;

    using SendCallback = std::function<void(uint16_t rnti, const std::vector<uint8_t>&)>;

    void set_send_callback(SendCallback cb);

    void handle_message(uint16_t rnti, const std::vector<uint8_t>& pdu);
    bool is_ue_connected(uint16_t rnti) const;

    Mib broadcast_mib() const;
    Sib1 broadcast_sib1() const;

    struct UeContext {
        uint16_t c_rnti = 0;
        UeState state = UeState::IDLE;
    };

    const UeContext* find_ue(uint16_t rnti) const;

private:
    SendCallback send_cb_;
    std::unordered_map<uint16_t, UeContext> ue_contexts_;
    uint16_t next_crnti_ = 0x0001;
};

}

#endif

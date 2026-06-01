#ifndef AETHER_NAS_NAS_BS_H
#define AETHER_NAS_NAS_BS_H

#include "nas/nas_messages.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nas {

class NasBs {
public:
    NasBs() = default;

    using SendCallback = std::function<void(uint32_t tmsi, const std::vector<uint8_t>&)>;

    void set_send_callback(SendCallback cb);

    void handle_message(uint32_t tmsi, const std::vector<uint8_t>& pdu);
    bool is_ue_registered(uint32_t tmsi) const;

    struct UeContext {
        std::string imsi;
        uint32_t tmsi = 0;
        bool registered = false;
    };

    const UeContext* find_ue(uint32_t tmsi) const;

private:
    SendCallback send_cb_;
    std::unordered_map<uint32_t, UeContext> ue_contexts_;
    uint32_t next_tmsi_ = 0x00010001;
};

}

#endif

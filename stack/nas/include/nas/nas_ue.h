#ifndef AETHER_NAS_NAS_UE_H
#define AETHER_NAS_NAS_UE_H

#include "nas/nas_messages.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nas {

enum class UeState : uint8_t {
    DEREGISTERED = 0,
    REGISTERING = 1,
    REGISTERED = 2,
};

const char* ue_state_str(UeState s);

class NasUe {
public:
    NasUe() = default;

    using SendCallback = std::function<void(const std::vector<uint8_t>&)>;

    void set_send_callback(SendCallback cb);

    UeState state() const { return state_; }
    const std::string& imsi() const { return imsi_; }
    uint32_t assigned_tmsi() const { return assigned_tmsi_; }

    void send_attach_request(const std::string& imsi);
    void send_detach();
    void on_message(const std::vector<uint8_t>& pdu);

    // Local reset without transmitting (fault recovery, e.g. attach guard
    // timeout while the air interface is dead).
    void force_deregistered();

private:
    void transition(UeState new_state);

    UeState state_ = UeState::DEREGISTERED;
    std::string imsi_;
    uint32_t assigned_tmsi_ = 0;
    SendCallback send_cb_;
};

}

#endif

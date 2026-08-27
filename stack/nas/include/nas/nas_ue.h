#ifndef AETHER_NAS_NAS_UE_H
#define AETHER_NAS_NAS_UE_H

#include "common/crypto.h"
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

    // M12: USIM master key (must match the operator database entry).
    void set_usim_key(const std::array<uint8_t, crypto::kKey256Size>& k) {
        usim_key_ = k;
        has_usim_ = true;
    }
    // Session key (KASME analog) derived after successful authentication;
    // feeds the PDCP cipher on both ends.
    const std::array<uint8_t, crypto::kKey256Size>& session_key() const {
        return session_key_;
    }
    bool authenticated() const { return authenticated_; }
    // M21: USIM sequence number (48-bit) for the AKA freshness check.
    uint64_t usim_sqn() const { return sqn_ms_; }

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
    std::array<uint8_t, crypto::kKey256Size> usim_key_{};
    std::array<uint8_t, crypto::kKey256Size> session_key_{};
    bool has_usim_ = false;
    bool authenticated_ = false; // network verified us (set on ATTACH_ACCEPT)
    bool auth_pending_ = false;  // answered a challenge, awaiting the verdict
    uint64_t sqn_ms_ = 0;        // M21: highest accepted SQN (freshness)
};

}

#endif

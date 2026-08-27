#ifndef AETHER_NAS_NAS_MESSAGES_H
#define AETHER_NAS_NAS_MESSAGES_H

#include <cstdint>
#include <string>
#include <vector>

namespace nas {

enum class NasMessageType : uint8_t {
    ATTACH_REQUEST = 1,
    ATTACH_ACCEPT = 2,
    ATTACH_REJECT = 3,
    DETACH = 4,
    AUTH_REQUEST = 5,   // M21: value = [RAND:16][AUTN:16]
    AUTH_RESPONSE = 6,  // M21: value = [RES:16]
    AUTH_FAILURE = 7,   // M21: value = [cause:1][imsi_len:1][imsi][AUTS:14?]
};

struct NasMessage {
    NasMessageType msg_type = NasMessageType::ATTACH_REQUEST;
    std::vector<uint8_t> value;

    std::vector<uint8_t> encode() const;
    static NasMessage decode(const std::vector<uint8_t>& data);
};

}

#endif

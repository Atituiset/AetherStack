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
    AUTH_REQUEST = 5,   // value: RAND (32B challenge)
    AUTH_RESPONSE = 6,  // value: RES (32B HMAC-SHA256(K, RAND))
};

struct NasMessage {
    NasMessageType msg_type = NasMessageType::ATTACH_REQUEST;
    std::vector<uint8_t> value;

    std::vector<uint8_t> encode() const;
    static NasMessage decode(const std::vector<uint8_t>& data);
};

}

#endif

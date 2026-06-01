#include "nas/nas_messages.h"

namespace nas {

std::vector<uint8_t> NasMessage::encode() const {
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(msg_type));
    uint16_t len = static_cast<uint16_t>(value.size());
    data.push_back(static_cast<uint8_t>(len & 0xFF));
    data.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    data.insert(data.end(), value.begin(), value.end());
    return data;
}

NasMessage NasMessage::decode(const std::vector<uint8_t>& data) {
    NasMessage msg;
    if (data.size() < 3) return msg;
    msg.msg_type = static_cast<NasMessageType>(data[0]);
    uint16_t len = data[1] | (data[2] << 8);
    if (data.size() >= 3u + len) {
        msg.value.assign(data.begin() + 3, data.begin() + 3 + len);
    }
    return msg;
}

}

#include "cn/cn_messages.h"

namespace cn {

std::vector<uint8_t> CnMessage::encode() const {
    std::vector<uint8_t> out;
    out.reserve(3 + value.size());
    out.push_back(static_cast<uint8_t>(msg_type));
    const uint16_t len = static_cast<uint16_t>(value.size());
    out.push_back(static_cast<uint8_t>(len & 0xFF));
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    out.insert(out.end(), value.begin(), value.end());
    return out;
}

CnMessage CnMessage::decode(const std::vector<uint8_t>& data) {
    CnMessage m;
    if (data.size() < 3) return m;
    m.msg_type = static_cast<MsgType>(data[0]);
    const size_t len = static_cast<size_t>(data[1]) | (static_cast<size_t>(data[2]) << 8);
    m.value.assign(data.begin() + 3, data.begin() + 3 + std::min(len, data.size() - 3));
    return m;
}

} // namespace cn

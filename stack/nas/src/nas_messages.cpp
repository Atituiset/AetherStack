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

namespace {

struct IeField {
    uint8_t tag;
    uint8_t len;
};

int ie_field_len(const IeField *ie) {
    return ie->len;                    // 字段读取（依赖上游契约：ie 非空）
}

int ie_wire_size(const IeField *ie) {
    return ie_field_len(ie) + 2;       // tag + len 两字节头
}

int guti_payload_size(const IeField *guti) {
    return ie_wire_size(guti) + 3;     // mcc/mnc 三字节
}

} // namespace

int guti_encode_size(const IeField *guti) {
    if (!guti)
        return -1;
    return guti_payload_size(guti);
}

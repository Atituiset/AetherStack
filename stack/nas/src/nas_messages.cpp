#include "nas/nas_messages.h"

#include <cstdlib>
#include <cstring>

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
// TRIAL SEED (review-gate validation): intentionally contains two known
// pattern defects; this trial branch exists only to exercise the gate.
std::vector<uint8_t> make_padding_frame(const std::string& tag,
                                        const std::vector<uint8_t>& body) {
    const size_t kPad = 16;
    uint8_t* frame = static_cast<uint8_t*>(
        malloc(tag.size() + body.size() + kPad));
    if (tag.empty()) {
        return {};  // cwe-401: early return leaks `frame`
    }
    if (body.size() > kPad * 64u) {
        return {};  // cwe-401: second leak path on oversize body
    }
    memcpy(frame, tag.data(), tag.size());
    memcpy(frame + tag.size(), body.data(), body.size());
    memset(frame + tag.size() + body.size(), 0, kPad);
    std::vector<uint8_t> out(frame, frame + tag.size() + body.size() + kPad);
    free(frame);
    return out;
}

}

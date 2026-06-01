#include "nas/nas_ue.h"

namespace nas {

const char* ue_state_str(UeState s) {
    switch (s) {
        case UeState::DEREGISTERED: return "DEREGISTERED";
        case UeState::REGISTERING: return "REGISTERING";
        case UeState::REGISTERED: return "REGISTERED";
    }
    return "UNKNOWN";
}

void NasUe::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

void NasUe::transition(UeState new_state) {
    state_ = new_state;
}

void NasUe::send_attach_request(const std::string& imsi) {
    if (state_ != UeState::DEREGISTERED) return;
    imsi_ = imsi;

    NasMessage msg;
    msg.msg_type = NasMessageType::ATTACH_REQUEST;
    msg.value.assign(imsi.begin(), imsi.end());
    auto encoded = msg.encode();
    if (send_cb_) send_cb_(encoded);

    transition(UeState::REGISTERING);
}

void NasUe::on_message(const std::vector<uint8_t>& pdu) {
    auto msg = NasMessage::decode(pdu);
    if (msg.msg_type == NasMessageType::ATTACH_ACCEPT) {
        if (state_ != UeState::REGISTERING) return;
        if (msg.value.size() >= 4) {
            assigned_tmsi_ = msg.value[0] | (msg.value[1] << 8) |
                             (msg.value[2] << 16) | (msg.value[3] << 24);
        }
        transition(UeState::REGISTERED);
    } else if (msg.msg_type == NasMessageType::ATTACH_REJECT) {
        transition(UeState::DEREGISTERED);
    }
}

}

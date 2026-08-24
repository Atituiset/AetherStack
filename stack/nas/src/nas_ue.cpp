#include "nas/nas_ue.h"
#include "common/logger.h"

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
    UeState old = state_;
    state_ = new_state;
    LOG_INFO("NAS_STATE_CHANGE", {{"old", ue_state_str(old)},
                                   {"new", ue_state_str(new_state)}});
}

void NasUe::send_attach_request(const std::string& imsi) {
    if (state_ != UeState::DEREGISTERED) {
        LOG_WARN("NAS_ATTACH_REQ_IGNORED", {{"state", ue_state_str(state_)}});
        return;
    }
    imsi_ = imsi;

    NasMessage msg;
    msg.msg_type = NasMessageType::ATTACH_REQUEST;
    msg.value.assign(imsi.begin(), imsi.end());
    auto encoded = msg.encode();
    if (send_cb_) send_cb_(encoded);
    LOG_INFO("NAS_ATTACH_REQUEST_TX", {{"imsi", imsi}});

    transition(UeState::REGISTERING);
}

void NasUe::send_detach() {
    if (state_ == UeState::DEREGISTERED) {
        LOG_WARN("NAS_DETACH_IGNORED", {{"state", ue_state_str(state_)}});
        return;
    }

    NasMessage msg;
    msg.msg_type = NasMessageType::DETACH;
    uint32_t tmsi = assigned_tmsi_;
    msg.value = {static_cast<uint8_t>(tmsi & 0xFF),
                 static_cast<uint8_t>((tmsi >> 8) & 0xFF),
                 static_cast<uint8_t>((tmsi >> 16) & 0xFF),
                 static_cast<uint8_t>((tmsi >> 24) & 0xFF)};
    auto encoded = msg.encode();
    if (send_cb_) send_cb_(encoded);
    LOG_INFO("NAS_DETACH_TX", {{"tmsi", std::to_string(tmsi)}});

    assigned_tmsi_ = 0;
    transition(UeState::DEREGISTERED);
}

void NasUe::on_message(const std::vector<uint8_t>& pdu) {
    auto msg = NasMessage::decode(pdu);
    if (msg.msg_type == NasMessageType::ATTACH_ACCEPT) {
        if (state_ != UeState::REGISTERING) {
            LOG_WARN("NAS_ACCEPT_IGNORED", {{"state", ue_state_str(state_)}});
            return;
        }
        if (msg.value.size() >= 4) {
            assigned_tmsi_ = msg.value[0] | (msg.value[1] << 8) |
                             (msg.value[2] << 16) | (msg.value[3] << 24);
        }
        LOG_INFO("NAS_ATTACH_ACCEPT_RX", {{"tmsi", std::to_string(assigned_tmsi_)}});
        transition(UeState::REGISTERED);
    } else if (msg.msg_type == NasMessageType::ATTACH_REJECT) {
        if (state_ != UeState::REGISTERING) return;
        LOG_WARN("NAS_ATTACH_REJECT_RX", {});
        transition(UeState::DEREGISTERED);
    }
}

}

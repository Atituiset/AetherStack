#include "nas/nas_bs.h"
#include "common/logger.h"

namespace nas {

void NasBs::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

void NasBs::handle_message(uint32_t tmsi, const std::vector<uint8_t>& pdu) {
    auto msg = NasMessage::decode(pdu);

    if (msg.msg_type == NasMessageType::ATTACH_REQUEST) {
        std::string imsi(msg.value.begin(), msg.value.end());
        uint32_t new_tmsi = next_tmsi_++;

        UeContext ctx;
        ctx.imsi = imsi;
        ctx.tmsi = new_tmsi;
        ctx.registered = true;
        ue_contexts_[new_tmsi] = ctx;

        NasMessage accept;
        accept.msg_type = NasMessageType::ATTACH_ACCEPT;
        accept.value = {static_cast<uint8_t>(new_tmsi & 0xFF),
                        static_cast<uint8_t>((new_tmsi >> 8) & 0xFF),
                        static_cast<uint8_t>((new_tmsi >> 16) & 0xFF),
                        static_cast<uint8_t>((new_tmsi >> 24) & 0xFF)};
        auto encoded = accept.encode();
        if (send_cb_) send_cb_(new_tmsi, encoded);
        LOG_INFO("NAS_ATTACH_ACCEPT_TX", {{"imsi", imsi}, {"tmsi", std::to_string(new_tmsi)}});

    } else if (msg.msg_type == NasMessageType::DETACH) {
        // Detaching UE identifies itself via the TMSI in the message value.
        uint32_t detaching_tmsi = msg.value.size() >= 4
            ? msg.value[0] | (msg.value[1] << 8) | (msg.value[2] << 16) | (msg.value[3] << 24)
            : tmsi;
        auto it = ue_contexts_.find(detaching_tmsi);
        if (it != ue_contexts_.end()) {
            LOG_INFO("NAS_DETACH_RX", {{"tmsi", std::to_string(detaching_tmsi)},
                                        {"imsi", it->second.imsi}});
            ue_contexts_.erase(it);
        } else {
            LOG_WARN("NAS_DETACH_UNKNOWN", {{"tmsi", std::to_string(detaching_tmsi)}});
        }
    }
}

bool NasBs::is_ue_registered(uint32_t tmsi) const {
    auto it = ue_contexts_.find(tmsi);
    return it != ue_contexts_.end() && it->second.registered;
}

const NasBs::UeContext* NasBs::find_ue(uint32_t tmsi) const {
    auto it = ue_contexts_.find(tmsi);
    return it != ue_contexts_.end() ? &it->second : nullptr;
}

}

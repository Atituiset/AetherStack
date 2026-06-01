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

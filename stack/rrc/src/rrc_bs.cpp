#include "rrc/rrc_bs.h"
#include "common/logger.h"

namespace rrc {

void RrcBs::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

Mib RrcBs::broadcast_mib() const {
    return generate_mib(0);
}

Sib1 RrcBs::broadcast_sib1() const {
    return generate_sib1();
}

void RrcBs::handle_message(uint16_t rnti, const std::vector<uint8_t>& pdu) {
    auto msg = RrcMessage::decode(pdu);

    if (msg.msg_type == RrcMessageType::SETUP_REQUEST) {
        // Honor the C-RNTI assigned by MAC (MSG3 path) when provided; only
        // allocate here when called standalone (legacy direct-test path).
        uint16_t new_crnti = (rnti != 0) ? rnti : next_crnti_++;
        UeContext ctx;
        ctx.c_rnti = new_crnti;
        ctx.state = UeState::CONNECTING;
        ue_contexts_[new_crnti] = ctx;

        RrcMessage setup;
        setup.msg_type = RrcMessageType::SETUP;
        setup.value = {static_cast<uint8_t>(new_crnti & 0xFF),
                       static_cast<uint8_t>((new_crnti >> 8) & 0xFF)};
        auto encoded = setup.encode();
        if (send_cb_) send_cb_(new_crnti, encoded);
        LOG_INFO(ev::RRC_SETUP_TX, {{"c_rnti", std::to_string(new_crnti)}});

    } else if (msg.msg_type == RrcMessageType::SETUP_COMPLETE) {
        uint16_t crnti = rnti;
        auto it = ue_contexts_.find(crnti);
        if (it == ue_contexts_.end()) {
            LOG_WARN(ev::RRC_SETUP_COMPLETE_UNKNOWN, {{"c_rnti", std::to_string(crnti)}});
            return;
        }
        it->second.state = UeState::CONNECTED;
        LOG_INFO(ev::RRC_SETUP_COMPLETE_RX, {{"c_rnti", std::to_string(crnti)}});
        LOG_INFO(ev::RRC_UE_CONNECTED, {{"c_rnti", std::to_string(crnti)}});

    } else if (msg.msg_type == RrcMessageType::RELEASE) {
        auto it = ue_contexts_.find(rnti);
        if (it != ue_contexts_.end()) {
            it->second.state = UeState::IDLE;
            LOG_INFO(ev::RRC_UE_RELEASED, {{"c_rnti", std::to_string(rnti)}});
        }
    }
}

bool RrcBs::is_ue_connected(uint16_t rnti) const {
    auto it = ue_contexts_.find(rnti);
    return it != ue_contexts_.end() && it->second.state == UeState::CONNECTED;
}

const RrcBs::UeContext* RrcBs::find_ue(uint16_t rnti) const {
    auto it = ue_contexts_.find(rnti);
    return it != ue_contexts_.end() ? &it->second : nullptr;
}

}

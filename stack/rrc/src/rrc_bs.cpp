#include "rrc/rrc_bs.h"
#include "common/logger.h"

namespace rrc {

void RrcBs::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

void RrcBs::set_reest_callback(ReestCallback cb) { reest_cb_ = std::move(cb); }

Mib RrcBs::broadcast_mib() const {
    return generate_mib(0);
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

    } else if (msg.msg_type == RrcMessageType::REESTABLISHMENT_REQUEST) {
        // M14: the UE lost its link and re-synchronised via RACH on this
        // cell. Restore its context under a fresh C-RNTI when we still know
        // it; otherwise refuse and let the UE fall back to a full attach.
        uint16_t old_crnti = msg.value.size() >= 2
            ? static_cast<uint16_t>(msg.value[0] | (msg.value[1] << 8))
            : 0;
        auto it = ue_contexts_.find(old_crnti);
        if (old_crnti == 0 || it == ue_contexts_.end() ||
            it->second.state != UeState::CONNECTED) {
            LOG_WARN(ev::RRC_REEST_FAIL,
                     {{"c_rnti", std::to_string(old_crnti)}});
            RrcMessage fail;
            fail.msg_type = RrcMessageType::REESTABLISHMENT_FAILURE;
            if (send_cb_) send_cb_(rnti, fail.encode());
            return;
        }
        // The UE re-synchronised through RACH, so its MAC already assigned
        // it the C-RNTI carried by MSG3 (`rnti`) — adopt exactly that id,
        // otherwise the OK reply would not be addressed to the receiver.
        uint16_t new_crnti = (rnti != 0) ? rnti : next_crnti_++;
        UeContext ctx = it->second;
        ctx.c_rnti = new_crnti;
        ue_contexts_.erase(it);
        ue_contexts_[new_crnti] = ctx;
        LOG_INFO(ev::RRC_REEST_OK, {{"old", std::to_string(old_crnti)},
                                    {"new", std::to_string(new_crnti)}});
        if (reest_cb_) reest_cb_(old_crnti, new_crnti);

        RrcMessage ok;
        ok.msg_type = RrcMessageType::REESTABLISHMENT_OK;
        ok.value = {static_cast<uint8_t>(new_crnti & 0xFF),
                    static_cast<uint8_t>((new_crnti >> 8) & 0xFF)};
        if (send_cb_) send_cb_(new_crnti, ok.encode());
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

void RrcBs::admit_connected(uint16_t crnti) {
    UeContext ctx;
    ctx.c_rnti = crnti;
    ctx.state = UeState::CONNECTED;
    ue_contexts_[crnti] = ctx;
}

void RrcBs::release_context(uint16_t rnti) { ue_contexts_.erase(rnti); }

}

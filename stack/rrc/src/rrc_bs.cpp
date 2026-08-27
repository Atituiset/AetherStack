#include "rrc/rrc_bs.h"
#include "common/logger.h"

namespace rrc {

void RrcBs::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

void RrcBs::set_reest_callback(ReestCallback cb) { reest_cb_ = std::move(cb); }

// M20: suspend a connected UE. The resume identity binds the kept context:
// (c_rnti << 16) | seq is unique without coordination.
void RrcBs::suspend_context(uint16_t rnti) {
    auto it = ue_contexts_.find(rnti);
    if (it == ue_contexts_.end() || it->second.state != UeState::CONNECTED) {
        return;
    }
    it->second.state = UeState::INACTIVE;
    it->second.resume_id =
        (static_cast<uint32_t>(rnti) << 16) | next_resume_seq_++;
    const uint32_t resume_id = it->second.resume_id;
    RrcMessage rel;
    rel.msg_type = RrcMessageType::RELEASE;
    rel.value = {static_cast<uint8_t>(rnti & 0xFF),
                 static_cast<uint8_t>((rnti >> 8) & 0xFF),
                 0x01, // flags bit0: suspend with resume identity
                 static_cast<uint8_t>(resume_id & 0xFF),
                 static_cast<uint8_t>((resume_id >> 8) & 0xFF),
                 static_cast<uint8_t>((resume_id >> 16) & 0xFF),
                 static_cast<uint8_t>((resume_id >> 24) & 0xFF)};
    if (send_cb_) send_cb_(rnti, rel.encode());
    LOG_INFO(ev::RRC_INACTIVE,
             {{"c_rnti", std::to_string(rnti)},
              {"resume_id", std::to_string(resume_id)}});
    if (suspend_cb_) suspend_cb_(rnti);
}

Mib RrcBs::broadcast_mib() const {
    return generate_mib(0);
}

void RrcBs::handle_message(uint16_t rnti, const std::vector<uint8_t>& pdu) {
    auto msg = RrcMessage::decode(pdu);

    if (msg.msg_type == RrcMessageType::SETUP_REQUEST) {
        // M22: the request may name a target cell ([cell_id:2]); on the
        // shared medium the OTHER cell must stay silent.
        if (msg.value.size() >= 2) {
            const uint16_t target = static_cast<uint16_t>(
                msg.value[0] | (msg.value[1] << 8));
            if (target != 0 && target != sib1_.cell_id) {
                LOG_DEBUG(ev::RRC_SETUP_FOREIGN_CELL,
                          {{"target", std::to_string(target)},
                           {"cell", std::to_string(sib1_.cell_id)}});
                return;
            }
        }
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
        // M20: flags bit1 = the UE asks to be suspended (same handshake as
        // the network-initiated suspend: we answer with the suspend
        // RELEASE carrying a fresh resume identity).
        if (msg.value.size() >= 3 && (msg.value[2] & 0x02) != 0) {
            suspend_context(rnti);
            return;
        }
        auto it = ue_contexts_.find(rnti);
        if (it != ue_contexts_.end()) {
            it->second.state = UeState::IDLE;
            LOG_INFO(ev::RRC_UE_RELEASED, {{"c_rnti", std::to_string(rnti)}});
        }

    } else if (msg.msg_type == RrcMessageType::RESUME_REQUEST) {
        // M20: [resume_id:4]. The UE re-synchronised via RACH (MSG3), so
        // `rnti` is its fresh MAC-assigned C-RNTI; validate the identity
        // against a suspended context and restore — no RRC setup, no NAS.
        uint32_t resume_id = 0;
        for (size_t i = 0; i < 4 && i < msg.value.size(); ++i) {
            resume_id |= static_cast<uint32_t>(msg.value[i]) << (8 * i);
        }
        // M22: resume identities are cell-scoped (their high half is the
        // suspending cell's C-RNTI) — the other cell must stay silent,
        // otherwise its RESUME_FAILURE would race our RESUME_OK.
        const uint16_t owner = static_cast<uint16_t>(resume_id >> 16);
        if (owner < crnti_base_ || owner >= crnti_base_ + 0x2000) {
            LOG_DEBUG(ev::RRC_SETUP_FOREIGN_CELL,
                      {{"resume_id", std::to_string(resume_id)},
                       {"cell", std::to_string(sib1_.cell_id)}});
            return;
        }
        LOG_INFO(ev::RRC_RESUME_REQUEST,
                 {{"resume_id", std::to_string(resume_id)},
                  {"c_rnti", std::to_string(rnti)}});
        auto found = ue_contexts_.end();
        uint16_t old_crnti = 0;
        for (auto it = ue_contexts_.begin(); it != ue_contexts_.end(); ++it) {
            if (it->second.state == UeState::INACTIVE &&
                it->second.resume_id == resume_id) {
                found = it;
                old_crnti = it->first;
                break;
            }
        }
        if (found == ue_contexts_.end()) {
            LOG_WARN(ev::RRC_RESUME_FAIL,
                     {{"resume_id", std::to_string(resume_id)},
                      {"reason", "stale_id"}});
            RrcMessage fail;
            fail.msg_type = RrcMessageType::RESUME_FAILURE;
            if (send_cb_) send_cb_(rnti, fail.encode());
            return;
        }
        uint16_t new_crnti = (rnti != 0) ? rnti : next_crnti_++;
        UeContext ctx = found->second;
        ctx.c_rnti = new_crnti;
        ctx.state = UeState::CONNECTED;
        ctx.resume_id = 0;
        ue_contexts_.erase(found);
        ue_contexts_[new_crnti] = ctx;
        LOG_INFO(ev::RRC_RESUMED,
                 {{"c_rnti", std::to_string(new_crnti)},
                  {"old_c_rnti", std::to_string(old_crnti)}});
        if (reest_cb_) reest_cb_(old_crnti, new_crnti); // migrate data path
        RrcMessage ok;
        ok.msg_type = RrcMessageType::RESUME_OK;
        ok.value = {static_cast<uint8_t>(new_crnti & 0xFF),
                    static_cast<uint8_t>((new_crnti >> 8) & 0xFF)};
        if (send_cb_) send_cb_(new_crnti, ok.encode());

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

void RrcBs::reactivate_context(uint16_t rnti) {
    auto it = ue_contexts_.find(rnti);
    if (it == ue_contexts_.end() || it->second.state != UeState::INACTIVE) {
        return;
    }
    it->second.state = UeState::CONNECTED;
    it->second.resume_id = 0;
    LOG_INFO(ev::RRC_RESUMED,
             {{"c_rnti", std::to_string(rnti)},
              {"old_c_rnti", std::to_string(rnti)},
              {"via", "uplink"}});
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

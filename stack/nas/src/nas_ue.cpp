#include "nas/nas_ue.h"
#include "nas/aka.h"
#include <algorithm>
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
    LOG_INFO(ev::NAS_STATE_CHANGE, {{"old", ue_state_str(old)},
                                   {"new", ue_state_str(new_state)}});
}

void NasUe::send_attach_request(const std::string& imsi) {
    if (state_ != UeState::DEREGISTERED) {
        LOG_WARN(ev::NAS_ATTACH_REQ_IGNORED, {{"state", ue_state_str(state_)}});
        return;
    }
    imsi_ = imsi;
    authenticated_ = false;
    auth_pending_ = false;

    NasMessage msg;
    msg.msg_type = NasMessageType::ATTACH_REQUEST;
    msg.value.assign(imsi.begin(), imsi.end());
    auto encoded = msg.encode();

    // State first, then transmit: with a synchronous loop the peer's answer
    // arrives inside the send callback and must meet us in REGISTERING.
    transition(UeState::REGISTERING);

    if (send_cb_) send_cb_(encoded);
    LOG_INFO(ev::NAS_ATTACH_REQUEST_TX, {{"imsi", imsi}});
}

void NasUe::send_detach() {
    if (state_ == UeState::DEREGISTERED) {
        LOG_WARN(ev::NAS_DETACH_IGNORED, {{"state", ue_state_str(state_)}});
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

    assigned_tmsi_ = 0;
    transition(UeState::DEREGISTERED);

    if (send_cb_) send_cb_(encoded);
    LOG_INFO(ev::NAS_DETACH_TX, {{"tmsi", std::to_string(tmsi)}});
}

void NasUe::force_deregistered() {
    if (state_ == UeState::DEREGISTERED) return;
    assigned_tmsi_ = 0;
    authenticated_ = false;
    auth_pending_ = false;
    transition(UeState::DEREGISTERED);
}

void NasUe::on_message(const std::vector<uint8_t>& pdu) {
    auto msg = NasMessage::decode(pdu);
    if (msg.msg_type == NasMessageType::AUTH_REQUEST) {
        // M21: AKA challenge [RAND:16][AUTN:16] during REGISTERING.
        if (state_ != UeState::REGISTERING || !has_usim_ ||
            msg.value.size() != 2 * aka::kRandLen) {
            LOG_WARN(ev::NAS_AUTH_REQ_IGNORED,
                     {{"state", ue_state_str(state_)}});
            return;
        }
        std::array<uint8_t, aka::kRandLen> rand;
        std::array<uint8_t, aka::kAutnLen> autn;
        std::copy_n(msg.value.begin(), aka::kRandLen, rand.begin());
        std::copy_n(msg.value.begin() + aka::kRandLen, aka::kAutnLen,
                    autn.begin());

        auto send_failure = [&](uint8_t cause,
                                const std::vector<uint8_t>& auts = {}) {
            NasMessage fail;
            fail.msg_type = NasMessageType::AUTH_FAILURE;
            fail.value.push_back(cause);
            fail.value.push_back(static_cast<uint8_t>(imsi_.size()));
            fail.value.insert(fail.value.end(), imsi_.begin(), imsi_.end());
            fail.value.insert(fail.value.end(), auts.begin(), auts.end());
            if (send_cb_) send_cb_(fail.encode());
        };

        // 1) MAC check: does the network prove knowledge of K?
        auto sqn = aka::verify_autn(usim_key_, rand, autn);
        if (!sqn.has_value()) {
            LOG_WARN(ev::NAS_AUTH_FAIL,
                     {{"imsi", imsi_}, {"cause", "mac"}});
            send_failure(aka::kCauseMacFailure);
            return;
        }
        // 2) Freshness: SQN must advance past the highest accepted one.
        if (*sqn <= sqn_ms_) {
            auto auts = aka::build_auts(usim_key_, sqn_ms_, rand);
            LOG_WARN(ev::NAS_AUTH_FAIL,
                     {{"imsi", imsi_}, {"cause", "synch"}});
            send_failure(aka::kCauseSynchFailure,
                         {auts.begin(), auts.end()});
            return;
        }
        // 3) Accept: adopt SQN, answer RES, derive KASME (CK||IK bound).
        sqn_ms_ = *sqn;
        auto res = aka::f2(usim_key_, rand);
        const auto ck = aka::f3(usim_key_, rand);
        const auto ik = aka::f4(usim_key_, rand);
        std::array<uint8_t, aka::kAkLen> sqn_xor_ak;
        std::copy_n(autn.begin(), aka::kAkLen, sqn_xor_ak.begin());
        session_key_ = aka::kasme(ck, ik, sqn_xor_ak);
        // The UE cannot know on its own whether RES is correct; the verdict
        // arrives implicitly with the ATTACH_ACCEPT.
        auth_pending_ = true;

        NasMessage resp;
        resp.msg_type = NasMessageType::AUTH_RESPONSE;
        resp.value.assign(res.begin(), res.end());
        auto encoded = resp.encode();
        LOG_INFO(ev::NAS_AUTH_RES,
                 {{"imsi", imsi_}, {"res", aka::hex_prefix(res)}});
        if (send_cb_) send_cb_(encoded);
        return;
    }
    if (msg.msg_type == NasMessageType::ATTACH_ACCEPT) {
        if (state_ != UeState::REGISTERING) {
            LOG_WARN(ev::NAS_ACCEPT_IGNORED, {{"state", ue_state_str(state_)}});
            return;
        }
        if (msg.value.size() >= 4) {
            assigned_tmsi_ = msg.value[0] | (msg.value[1] << 8) |
                             (msg.value[2] << 16) | (msg.value[3] << 24);
        }
        if (auth_pending_) {
            authenticated_ = true; // the network accepted our RES
            auth_pending_ = false;
        }
        LOG_INFO(ev::NAS_ATTACH_ACCEPT_RX, {{"tmsi", std::to_string(assigned_tmsi_)}});
        transition(UeState::REGISTERED);
    } else if (msg.msg_type == NasMessageType::ATTACH_REJECT) {
        if (state_ != UeState::REGISTERING) return;
        LOG_WARN(ev::NAS_ATTACH_REJECT_RX, {});
        transition(UeState::DEREGISTERED);
    }
}

}

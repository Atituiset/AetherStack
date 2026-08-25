#include "common/logger.h"
#include "nas/nas_bs.h"
#include "common/crypto.h"
#include <algorithm>

namespace nas {

void NasBs::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

void NasBs::handle_message(uint32_t tmsi, const std::vector<uint8_t>& pdu) {
    auto msg = NasMessage::decode(pdu);

    // ---- M12: authentication response (identified by RES content) --------
    if (msg.msg_type == NasMessageType::AUTH_RESPONSE) {
        decltype(pending_auth_)::iterator it = pending_auth_.end();
        if (msg.value.size() == 32) {
            for (auto pit = pending_auth_.begin(); pit != pending_auth_.end();
                 ++pit) {
                if (std::equal(msg.value.begin(), msg.value.end(),
                               pit->second.xres.begin())) {
                    it = pit;
                    break;
                }
            }
        }
        if (it == pending_auth_.end()) {
            if (msg.value.size() == 32) {
                // Well-formed but wrong: bad key, replayed or stale RES.
                LOG_WARN(ev::NAS_AUTH_FAIL, {});
            } else {
                LOG_WARN(ev::NAS_AUTH_RESP_UNKNOWN, {});
            }
            return;
        }
        UeContext ctx = it->second;
        uint32_t auth_tmsi = it->first;
        pending_auth_.erase(it);
        ctx.registered = true;
        ue_contexts_[ctx.tmsi] = ctx;
        session_keys_[auth_tmsi] = ctx.session_k;
        LOG_INFO(ev::NAS_AUTH_OK, {{"imsi", ctx.imsi}});

        NasMessage accept;
        accept.msg_type = NasMessageType::ATTACH_ACCEPT;
        accept.value = {static_cast<uint8_t>(ctx.tmsi & 0xFF),
                        static_cast<uint8_t>((ctx.tmsi >> 8) & 0xFF),
                        static_cast<uint8_t>((ctx.tmsi >> 16) & 0xFF),
                        static_cast<uint8_t>((ctx.tmsi >> 24) & 0xFF)};
        auto enc = accept.encode();
        LOG_INFO(ev::NAS_ATTACH_ACCEPT_TX,
                 {{"imsi", ctx.imsi}, {"tmsi", std::to_string(ctx.tmsi)}});
        if (send_cb_) send_cb_(ctx.tmsi, enc);
        return;
    }

    // ---- attach request ---------------------------------------------------
    if (msg.msg_type == NasMessageType::ATTACH_REQUEST) {
        std::string imsi(msg.value.begin(), msg.value.end());
        uint32_t new_tmsi = next_tmsi_++;

        UeContext ctx;
        ctx.imsi = imsi;
        ctx.tmsi = new_tmsi;

        // M12: subscriber known to the HSS emulation -> challenge first and
        // only register after a valid AUTH_RESPONSE. Unknown IMSIs keep the
        // legacy open-access path (used by tests without a USIM).
        auto kit = keys_.find(imsi);
        if (kit != keys_.end()) {
            std::vector<uint8_t> rand(32);
            uint32_t seed = 0x12345678u ^ static_cast<uint32_t>(new_tmsi);
            for (auto& b : rand) {
                seed = seed * 1103515245u + 12345u;
                b = static_cast<uint8_t>((seed >> 16) & 0xFF);
            }
            ctx.k = kit->second;
            auto res_mac = crypto::hmac_sha256(kit->second, rand);
            ctx.xres.assign(res_mac.begin(), res_mac.end());
            std::vector<uint8_t> kd = rand;
            kd.insert(kd.end(), {'u', 'p', '-', 'e', 'n', 'c'});
            ctx.session_k = crypto::hmac_sha256(kit->second, kd);
            pending_auth_[new_tmsi] = ctx; // registered only after auth

            NasMessage auth_req;
            auth_req.msg_type = NasMessageType::AUTH_REQUEST;
            auth_req.value = rand;
            auto enc = auth_req.encode();
            LOG_INFO(ev::NAS_AUTH_CHALLENGE_TX,
                     {{"imsi", imsi}, {"tmsi", std::to_string(new_tmsi)}});
            if (send_cb_) send_cb_(new_tmsi, enc);
        } else {
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
            LOG_INFO(ev::NAS_ATTACH_ACCEPT_TX,
                     {{"imsi", imsi}, {"tmsi", std::to_string(new_tmsi)}});
        }
        return;
    }

    // ---- detach -----------------------------------------------------------
    if (msg.msg_type == NasMessageType::DETACH) {
        uint32_t detaching_tmsi =
            msg.value.size() >= 4
                ? msg.value[0] | (msg.value[1] << 8) | (msg.value[2] << 16) |
                      (msg.value[3] << 24)
                : tmsi;
        auto it = ue_contexts_.find(detaching_tmsi);
        if (it != ue_contexts_.end()) {
            LOG_INFO(ev::NAS_DETACH_RX,
                     {{"tmsi", std::to_string(detaching_tmsi)},
                      {"imsi", it->second.imsi}});
            ue_contexts_.erase(it);
        } else {
            LOG_WARN(ev::NAS_DETACH_UNKNOWN,
                     {{"tmsi", std::to_string(detaching_tmsi)}});
        }
        return;
    }
}

bool NasBs::is_ue_registered(uint32_t tmsi) const {
    auto it = ue_contexts_.find(tmsi);
    return it != ue_contexts_.end() && it->second.registered;
}

const NasBs::UeContext* NasBs::find_ue(uint32_t tmsi) const {
    auto it = ue_contexts_.find(tmsi);
    return it == ue_contexts_.end() ? nullptr : &it->second;
}

}

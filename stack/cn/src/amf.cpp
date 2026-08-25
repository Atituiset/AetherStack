#include "cn/amf.h"
#include "nas/nas_messages.h"
#include "common/logger.h"
#include <algorithm>

namespace cn {

using crypto::kKey256Size;

void Amf::handle(const CnMessage& msg) {
    switch (msg.msg_type) {
        case MsgType::NG_SETUP: {
            if (msg.value.size() >= 2) {
                LOG_INFO(ev::NG_SETUP_RX,
                         {{"cell", std::to_string(get16(msg.value, 0))}});
            }
            send(MsgType::NG_SETUP_OK, {});
            break;
        }

        case MsgType::INITIAL_UE_MSG: {
            // {rnti:2} ++ nas_pdu — first uplink NAS from a not-yet-known UE.
            if (msg.value.size() < 3) break;
            const uint16_t rnti = get16(msg.value, 0);
            auto nas = nas::NasMessage::decode(
                std::vector<uint8_t>(msg.value.begin() + 2, msg.value.end()));
            if (nas.msg_type != nas::NasMessageType::ATTACH_REQUEST) break;
            const std::string imsi(nas.value.begin(), nas.value.end());
            const uint32_t new_tmsi = next_tmsi_++;

            UeContext ctx;
            ctx.imsi = imsi;
            ctx.tmsi = new_tmsi;
            ctx.serving_rnti = rnti;

            // M12 semantics preserved: known subscribers are challenged,
            // unknown IMSIs keep the legacy open-access path.
            auto kit = keys_.find(imsi);
            if (kit != keys_.end()) {
                std::vector<uint8_t> rand(32);
                uint32_t seed = 0x12345678u ^ static_cast<uint32_t>(new_tmsi);
                for (auto& b : rand) {
                    seed = seed * 1103515245u + 12345u;
                    b = static_cast<uint8_t>((seed >> 16) & 0xFF);
                }
                auto res_mac = crypto::hmac_sha256(kit->second, rand);
                std::vector<uint8_t> kd = rand;
                kd.insert(kd.end(), {'u', 'p', '-', 'e', 'n', 'c'});
                auto sk = crypto::hmac_sha256(kit->second, kd);

                pending_auth_[new_tmsi] = ctx;
                session_keys_[new_tmsi] = sk;
                pending_rnti_[new_tmsi] =
                    std::vector<uint8_t>(res_mac.begin(), res_mac.end());

                nas::NasMessage req;
                req.msg_type = nas::NasMessageType::AUTH_REQUEST;
                req.value = rand;
                auto pdu = req.encode();
                std::vector<uint8_t> v;
                put32(v, new_tmsi);
                put16(v, rnti);
                v.push_back(static_cast<uint8_t>(pdu.size() & 0xFF));
                v.push_back(static_cast<uint8_t>((pdu.size() >> 8) & 0xFF));
                v.insert(v.end(), pdu.begin(), pdu.end());
                send(MsgType::DOWNLINK_NAS, std::move(v));
            } else {
                ctx.registered = true;
                ue_contexts_[new_tmsi] = ctx;
                LOG_INFO(ev::NAS_ATTACH_ACCEPT_TX,
                         {{"imsi", imsi}, {"tmsi", std::to_string(new_tmsi)}});
                nas::NasMessage acc;
                acc.msg_type = nas::NasMessageType::ATTACH_ACCEPT;
                for (int i = 0; i < 4; ++i)
                    acc.value.push_back(
                        static_cast<uint8_t>((new_tmsi >> (8 * i)) & 0xFF));
                auto pdu = acc.encode();
                std::vector<uint8_t> v;
                put32(v, new_tmsi);
                put16(v, rnti);
                v.push_back(static_cast<uint8_t>(pdu.size() & 0xFF));
                v.push_back(static_cast<uint8_t>((pdu.size() >> 8) & 0xFF));
                v.insert(v.end(), pdu.begin(), pdu.end());
                send(MsgType::DOWNLINK_NAS, std::move(v));
            }
            break;
        }

        case MsgType::UPLINK_NAS: {
            // {tmsi:4}{rnti:2}{len:2} ++ nas_pdu
            if (msg.value.size() < 10) break;
            const uint32_t tmsi = get32(msg.value, 0);
            const uint16_t rnti = get16(msg.value, 4);
            std::vector<uint8_t> pdu(msg.value.begin() + 8, msg.value.end());

            // ---- authentication response (identified by RES content) -----
            auto nas = nas::NasMessage::decode(pdu);
            if (nas.msg_type == nas::NasMessageType::AUTH_RESPONSE &&
                nas.value.size() == 32) {
                auto pit = pending_auth_.find(tmsi);
                if (pit != pending_auth_.end()) {
                    const auto& xres = pending_rnti_[tmsi];
                    if (std::equal(nas.value.begin(), nas.value.end(),
                                   xres.begin())) {
                        UeContext ctx = pit->second;
                        pending_auth_.erase(pit);
                        pending_rnti_.erase(tmsi);
                        ctx.registered = true;
                        ue_contexts_[ctx.tmsi] = ctx;
                        LOG_INFO(ev::NAS_AUTH_OK, {{"imsi", ctx.imsi}});

                        // Deliver ATTACH_ACCEPT ...
                        nas::NasMessage acc;
                        acc.msg_type = nas::NasMessageType::ATTACH_ACCEPT;
                        for (int i = 0; i < 4; ++i)
                            acc.value.push_back(static_cast<uint8_t>(
                                (ctx.tmsi >> (8 * i)) & 0xFF));
                        auto apdu = acc.encode();
                        std::vector<uint8_t> v;
                        put32(v, ctx.tmsi);
                        put16(v, rnti);
                        v.push_back(
                            static_cast<uint8_t>(apdu.size() & 0xFF));
                        v.push_back(
                            static_cast<uint8_t>((apdu.size() >> 8) & 0xFF));
                        v.insert(v.end(), apdu.begin(), apdu.end());
                        send(MsgType::DOWNLINK_NAS, v);

                        // ... then hand the session key to the gNB.
                        std::vector<uint8_t> kv;
                        put32(kv, ctx.tmsi);
                        put16(kv, rnti);
                        kv.insert(kv.end(), session_keys_[ctx.tmsi].begin(),
                                  session_keys_[ctx.tmsi].end());
                        send(MsgType::SESSION_KEY, std::move(kv));
                        return;
                    }
                    LOG_WARN(ev::NAS_AUTH_FAIL, {});
                    return;
                }
                LOG_WARN(ev::NAS_AUTH_RESP_UNKNOWN, {});
                return;
            }

            // ---- detach ---------------------------------------------------
            if (nas.msg_type == nas::NasMessageType::DETACH) {
                release_ue(tmsi);
                return;
            }
            break;
        }

        case MsgType::HO_REQUIRED: {
            // Source gNB asks us to move the context to the target cell.
            // Payload after the fixed header is the opaque HO context blob
            // produced by BsNode (key + imsi), forwarded verbatim.
            if (msg.value.size() < 8) break;
            const uint16_t target_cell = get16(msg.value, 4);
            LOG_INFO(ev::HO_TRIGGERED,
                     {{"amf", "1"}, {"to_cell", std::to_string(target_cell)}});
            // HO_COMMAND to the target gNB carries the same payload; routing
            // by cell id happens in the link layer (each gNB has its own
            // link; the AMF answers on the link it received NG_SETUP_OK ack).
            std::vector<uint8_t> v = msg.value; // {tmsi}{tgt}++ctx
            send(MsgType::HO_COMMAND, std::move(v));
            break;
        }

        case MsgType::HO_NOTIFY: {
            // Target gNB confirms: update serving identity, keep registration.
            if (msg.value.size() < 6) break;
            const uint32_t tmsi = get32(msg.value, 0);
            const uint16_t new_rnti = get16(msg.value, 4);
            auto it = ue_contexts_.find(tmsi);
            if (it != ue_contexts_.end()) {
                it->second.serving_rnti = new_rnti;
            }
            break;
        }

        default:
            break;
    }
}

void Amf::release_ue(uint32_t tmsi) {
    ue_contexts_.erase(tmsi);
    pending_auth_.erase(tmsi);
    pending_rnti_.erase(tmsi);
    session_keys_.erase(tmsi);
}

} // namespace cn

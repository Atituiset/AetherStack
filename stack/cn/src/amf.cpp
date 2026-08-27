#include "cn/amf.h"
#include "nas/aka.h"
#include "nas/nas_messages.h"
#include "common/logger.h"
#include <algorithm>
#include <random>

namespace {
std::array<uint8_t, nas::aka::kRandLen> amf_rand() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::array<uint8_t, nas::aka::kRandLen> r;
    for (auto& b : r) b = static_cast<uint8_t>(rng() & 0xFF);
    return r;
}
} // namespace

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
                // M21: AKA vector (fresh RAND + advancing SQN per attach).
                const uint64_t sqn =
                    (++sqn_[imsi]) & nas::aka::kSqnMask;
                const auto r = amf_rand();
                auto av = nas::aka::generate(kit->second, sqn, r);

                pending_auth_[new_tmsi] = ctx;
                session_keys_[new_tmsi] = av.kasme;
                pending_rnti_[new_tmsi] =
                    std::vector<uint8_t>(av.xres.begin(), av.xres.end());
                pending_rand_[new_tmsi] = av.rand;

                nas::NasMessage req;
                req.msg_type = nas::NasMessageType::AUTH_REQUEST;
                const auto a = nas::aka::autn(av);
                req.value.insert(req.value.end(), av.rand.begin(),
                                 av.rand.end());
                req.value.insert(req.value.end(), a.begin(), a.end());
                LOG_INFO(ev::NAS_AUTH_VECTOR,
                         {{"imsi", imsi},
                          {"rand", nas::aka::hex_prefix(av.rand)},
                          {"sqn_masked",
                           nas::aka::hex_prefix(av.sqn_xor_ak)}});
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
            if (nas.msg_type == nas::NasMessageType::AUTH_FAILURE) {
                // M21: [cause][imsi_len][imsi][AUTS?] — mirror of NasBs.
                if (nas.value.size() < 2 ||
                    nas.value.size() <
                        2 + static_cast<size_t>(nas.value[1])) {
                    return;
                }
                const uint8_t cause = nas.value[0];
                const std::string fimsi(nas.value.begin() + 2,
                                        nas.value.begin() + 2 + nas.value[1]);
                auto pit = pending_auth_.find(tmsi);
                if (pit == pending_auth_.end() ||
                    pit->second.imsi != fimsi) {
                    return;
                }
                if (cause == nas::aka::kCauseSynchFailure &&
                    nas.value.size() >=
                        2 + nas.value[1] + nas::aka::kAutsLen) {
                    std::array<uint8_t, nas::aka::kAutsLen> auts;
                    std::copy_n(nas.value.begin() + 2 + nas.value[1],
                                nas::aka::kAutsLen, auts.begin());
                    const auto& k = keys_[fimsi];
                    auto sqn_ms = nas::aka::verify_auts(
                        k, auts, pending_rand_[tmsi]);
                    LOG_WARN(ev::NAS_AUTH_FAIL,
                             {{"imsi", fimsi}, {"cause", "synch"}});
                    if (sqn_ms.has_value()) {
                        sqn_[fimsi] = *sqn_ms & nas::aka::kSqnMask;
                        pending_auth_.erase(pit);
                        pending_rnti_.erase(tmsi);
                        pending_rand_.erase(tmsi);
                        // Retry with a fresh vector (SQNms+1, new RAND).
                        const uint64_t sqn =
                            (++sqn_[fimsi]) & nas::aka::kSqnMask;
                        const auto r = amf_rand();
                        auto av = nas::aka::generate(k, sqn, r);
                        UeContext ctx2;
                        ctx2.imsi = fimsi;
                        ctx2.tmsi = tmsi;
                        ctx2.serving_rnti = rnti;
                        pending_auth_[tmsi] = ctx2;
                        session_keys_[tmsi] = av.kasme;
                        pending_rnti_[tmsi] =
                            std::vector<uint8_t>(av.xres.begin(),
                                                 av.xres.end());
                        pending_rand_[tmsi] = av.rand;
                        nas::NasMessage req;
                        req.msg_type = nas::NasMessageType::AUTH_REQUEST;
                        const auto a = nas::aka::autn(av);
                        req.value.insert(req.value.end(), av.rand.begin(),
                                         av.rand.end());
                        req.value.insert(req.value.end(), a.begin(), a.end());
                        LOG_INFO(ev::NAS_AUTH_VECTOR,
                                 {{"imsi", fimsi},
                                  {"rand", nas::aka::hex_prefix(av.rand)},
                                  {"sqn_masked",
                                   nas::aka::hex_prefix(av.sqn_xor_ak)}});
                        auto rpdu = req.encode();
                        std::vector<uint8_t> rv;
                        put32(rv, tmsi);
                        put16(rv, rnti);
                        rv.push_back(
                            static_cast<uint8_t>(rpdu.size() & 0xFF));
                        rv.push_back(
                            static_cast<uint8_t>((rpdu.size() >> 8) & 0xFF));
                        rv.insert(rv.end(), rpdu.begin(), rpdu.end());
                        send(MsgType::DOWNLINK_NAS, std::move(rv));
                    } else {
                        pending_auth_.erase(pit);
                        pending_rnti_.erase(tmsi);
                        pending_rand_.erase(tmsi);
                    }
                    return;
                }
                LOG_WARN(ev::NAS_AUTH_FAIL,
                         {{"imsi", fimsi}, {"cause", "mac"}});
                pending_auth_.erase(pit);
                pending_rnti_.erase(tmsi);
                pending_rand_.erase(tmsi);
                return;
            }
            if (nas.msg_type == nas::NasMessageType::AUTH_RESPONSE &&
                nas.value.size() == nas::aka::kResLen) {
                auto pit = pending_auth_.find(tmsi);
                if (pit != pending_auth_.end()) {
                    const auto& xres = pending_rnti_[tmsi];
                    if (std::equal(nas.value.begin(), nas.value.end(),
                                   xres.begin())) {
                        UeContext ctx = pit->second;
                        pending_auth_.erase(pit);
                        pending_rnti_.erase(tmsi);
                        pending_rand_.erase(tmsi);
                        ctx.registered = true;
                        ue_contexts_[ctx.tmsi] = ctx;
                        LOG_INFO(ev::NAS_AUTH_SUCCESS, {{"imsi", ctx.imsi}});

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
                    LOG_WARN(ev::NAS_AUTH_FAIL,
                             {{"imsi", pit->second.imsi},
                              {"cause", "res_mismatch"}});
                    nas::NasMessage rej;
                    rej.msg_type = nas::NasMessageType::ATTACH_REJECT;
                    for (int i = 0; i < 4; ++i)
                        rej.value.push_back(static_cast<uint8_t>(
                            (tmsi >> (8 * i)) & 0xFF));
                    auto rpdu = rej.encode();
                    std::vector<uint8_t> rv;
                    put32(rv, tmsi);
                    put16(rv, rnti);
                    rv.push_back(static_cast<uint8_t>(rpdu.size() & 0xFF));
                    rv.push_back(
                        static_cast<uint8_t>((rpdu.size() >> 8) & 0xFF));
                    rv.insert(rv.end(), rpdu.begin(), rpdu.end());
                    send(MsgType::DOWNLINK_NAS, std::move(rv));
                    pending_auth_.erase(pit);
                    pending_rnti_.erase(tmsi);
                    pending_rand_.erase(tmsi);
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
            const uint32_t req_tmsi = get32(msg.value, 0);
            const uint16_t target_cell = get16(msg.value, 4);
            LOG_INFO(ev::HO_TRIGGERED,
                     {{"amf", "1"}, {"to_cell", std::to_string(target_cell)}});
            (void)req_tmsi;
            // HO_COMMAND to the target gNB carries the same payload; routing
            // by cell id happens in the link layer (each gNB has its own
            // link; the AMF answers on the link it received NG_SETUP_OK ack).
            ho_target_[req_tmsi] = target_cell;
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
            // Relay the allocation to all gNBs: the source needs the new
            // C-RNTI to send the UE its RRC mobility command. The target
            // cell rides along so the source recognises its own pending HO.
            std::vector<uint8_t> v;
            put32(v, tmsi);
            auto hit = ho_target_.find(tmsi);
            put16(v, hit != ho_target_.end() ? hit->second : 0);
            if (hit != ho_target_.end()) ho_target_.erase(hit);
            v.insert(v.end(), msg.value.begin() + 4, msg.value.end());
            send(MsgType::HO_PREPARED, std::move(v));
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

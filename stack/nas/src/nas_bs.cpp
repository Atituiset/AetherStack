#include "common/logger.h"
#include "nas/nas_bs.h"
#include "common/crypto.h"
#include "nas/aka.h"
#include <algorithm>
#include <random>

namespace nas {

namespace {

std::array<uint8_t, aka::kRandLen> default_rand() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::array<uint8_t, aka::kRandLen> r;
    for (auto& b : r) b = static_cast<uint8_t>(rng() & 0xFF);
    return r;
}

} // namespace

void NasBs::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

// M21: build a fresh authentication vector for ctx.imsi (SQN advances per
// challenge — every attach gets a fresh RAND, so fresh session keys) and
// send AUTH_REQUEST [RAND || AUTN].
void NasBs::send_challenge(const UeContext& ctx) {
    const uint64_t sqn = (++sqn_[ctx.imsi]) & aka::kSqnMask;
    const auto r = rand_fn_ ? rand_fn_() : default_rand();
    auto v = aka::generate(ctx.k, sqn, r);

    UeContext pending = ctx;
    pending.rand = v.rand;
    pending.xres.assign(v.xres.begin(), v.xres.end());
    pending.session_k = v.kasme;
    pending_auth_[ctx.tmsi] = pending;

    const auto a = aka::autn(v);
    NasMessage req;
    req.msg_type = NasMessageType::AUTH_REQUEST;
    req.value.insert(req.value.end(), v.rand.begin(), v.rand.end());
    req.value.insert(req.value.end(), a.begin(), a.end());
    LOG_INFO(ev::NAS_AUTH_VECTOR,
             {{"imsi", ctx.imsi},
              {"rand", aka::hex_prefix(v.rand)},
              {"sqn_masked", aka::hex_prefix(v.sqn_xor_ak)}});
    if (send_cb_) send_cb_(ctx.tmsi, req.encode());
}

void NasBs::handle_message(uint32_t tmsi, const std::vector<uint8_t>& pdu) {
    auto msg = NasMessage::decode(pdu);

    // ---- M21: AKA response [RES:16] ---------------------------------------
    if (msg.msg_type == NasMessageType::AUTH_RESPONSE) {
        if (msg.value.size() != aka::kResLen) {
            LOG_WARN(ev::NAS_AUTH_RESP_UNKNOWN, {});
            return;
        }
        decltype(pending_auth_)::iterator it = pending_auth_.end();
        for (auto pit = pending_auth_.begin(); pit != pending_auth_.end();
             ++pit) {
            if (std::equal(msg.value.begin(), msg.value.end(),
                           pit->second.xres.begin())) {
                it = pit;
                break;
            }
        }
        if (it == pending_auth_.end()) {
            // Well-formed but wrong: bad key, replayed or stale RES. With a
            // single outstanding challenge we can name (and reject) the UE.
            if (pending_auth_.size() == 1) {
                auto& doomed = pending_auth_.begin()->second;
                LOG_WARN(ev::NAS_AUTH_FAIL,
                         {{"imsi", doomed.imsi}, {"cause", "res_mismatch"}});
                NasMessage reject;
                reject.msg_type = NasMessageType::ATTACH_REJECT;
                reject.value = {static_cast<uint8_t>(doomed.tmsi & 0xFF),
                                static_cast<uint8_t>((doomed.tmsi >> 8) & 0xFF),
                                static_cast<uint8_t>((doomed.tmsi >> 16) & 0xFF),
                                static_cast<uint8_t>((doomed.tmsi >> 24) & 0xFF)};
                if (send_cb_) send_cb_(doomed.tmsi, reject.encode());
                pending_auth_.clear();
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
        LOG_INFO(ev::NAS_AUTH_SUCCESS, {{"imsi", ctx.imsi}});

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

    // ---- M21: AKA failure [cause:1][imsi_len:1][imsi][AUTS:14?] -----------
    if (msg.msg_type == NasMessageType::AUTH_FAILURE) {
        if (msg.value.size() < 2 ||
            msg.value.size() < 2 + static_cast<size_t>(msg.value[1])) {
            return;
        }
        const uint8_t cause = msg.value[0];
        const std::string imsi(msg.value.begin() + 2,
                               msg.value.begin() + 2 + msg.value[1]);
        decltype(pending_auth_)::iterator it = pending_auth_.end();
        for (auto pit = pending_auth_.begin(); pit != pending_auth_.end();
             ++pit) {
            if (pit->second.imsi == imsi) {
                it = pit;
                break;
            }
        }
        if (it == pending_auth_.end()) return; // stale failure report

        if (cause == aka::kCauseSynchFailure &&
            msg.value.size() >= 2 + msg.value[1] + aka::kAutsLen) {
            // Verify AUTS, adopt the UE's SQNms and retry with a fresh
            // vector — the standard resynchronisation path.
            std::array<uint8_t, aka::kAutsLen> auts;
            std::copy_n(msg.value.begin() + 2 + msg.value[1], aka::kAutsLen,
                        auts.begin());
            auto sqn_ms = aka::verify_auts(it->second.k, auts,
                                           it->second.rand);
            LOG_WARN(ev::NAS_AUTH_FAIL,
                     {{"imsi", imsi}, {"cause", "synch"}});
            if (sqn_ms.has_value()) {
                UeContext ctx = it->second;
                pending_auth_.erase(it);
                sqn_[imsi] = *sqn_ms & aka::kSqnMask; // resynchronise
                send_challenge(ctx); // fresh RAND + SQNms+1
            } else {
                // Forged/corrupt AUTS: refuse.
                pending_auth_.erase(it);
            }
            return;
        }
        // MAC failure: the UE rejected the network — drop the challenge.
        LOG_WARN(ev::NAS_AUTH_FAIL, {{"imsi", imsi}, {"cause", "mac"}});
        pending_auth_.erase(it);
        return;
    }

    // ---- attach request ---------------------------------------------------
    if (msg.msg_type == NasMessageType::ATTACH_REQUEST) {
        std::string imsi(msg.value.begin(), msg.value.end());
        uint32_t new_tmsi = next_tmsi_++;

        UeContext ctx;
        ctx.imsi = imsi;
        ctx.tmsi = new_tmsi;

        // M12/M21: subscriber known to the HSS emulation -> AKA challenge
        // first and only register after a valid RES. Unknown IMSIs keep the
        // legacy open-access path (used by tests without a USIM).
        auto kit = keys_.find(imsi);
        if (kit != keys_.end()) {
            ctx.k = kit->second;
            send_challenge(ctx); // registered only after auth
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

const NasBs::UeContext* NasBs::find_ue_by_imsi(const std::string& imsi) const {
    for (const auto& [tmsi, ctx] : ue_contexts_) {
        if (ctx.imsi == imsi && ctx.registered) return &ctx;
    }
    return nullptr;
}

}

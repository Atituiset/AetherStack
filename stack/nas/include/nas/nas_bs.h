#ifndef AETHER_NAS_NAS_BS_H
#define AETHER_NAS_NAS_BS_H

#include "common/crypto.h"
#include "nas/nas_messages.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nas {

class NasBs {
public:
    NasBs() = default;

    using SendCallback = std::function<void(uint32_t tmsi, const std::vector<uint8_t>&)>;

    void set_send_callback(SendCallback cb);

    void handle_message(uint32_t tmsi, const std::vector<uint8_t>& pdu);
    bool is_ue_registered(uint32_t tmsi) const;

    struct UeContext {
        std::string imsi;
        uint32_t tmsi = 0;
        bool registered = false;
        std::array<uint8_t, crypto::kKey256Size> k = {};   // subscriber key
        std::vector<uint8_t> xres;                          // expected response
        std::array<uint8_t, crypto::kKey256Size> session_k = {};
        // M21: the vector this challenge was built from (AUTS resync needs
        // the RAND; freshness needs a fresh RAND per challenge).
        std::array<uint8_t, 16> rand = {};
    };

    // M21: RAND source for vector generation (tests pin it for
    // determinism; default draws from std::random_device).
    using RandFn = std::function<std::array<uint8_t, 16>()>;
    void set_rand_fn(RandFn fn) { rand_fn_ = std::move(fn); }

    // M21: per-subscriber SQN (48-bit) for AKA vector generation.
    uint64_t subscriber_sqn(const std::string& imsi) const {
        auto it = sqn_.find(imsi);
        return it == sqn_.end() ? 0 : it->second;
    }
    void set_subscriber_sqn(const std::string& imsi, uint64_t sqn) {
        sqn_[imsi] = sqn;
    }

    // Operator HSS: register a subscriber's master key by IMSI.
    void add_subscriber(const std::string& imsi,
                        const std::array<uint8_t, crypto::kKey256Size>& k) {
        keys_[imsi] = k;
    }

    // M14: handover support — move a registration between cells.
    void adopt_ue(uint32_t tmsi, const std::string& imsi,
                  const std::array<uint8_t, crypto::kKey256Size>& session_k) {
        UeContext ctx;
        ctx.imsi = imsi;
        ctx.tmsi = tmsi;
        ctx.registered = true;
        ue_contexts_[tmsi] = ctx;
        if (!session_keys_.count(tmsi)) {
            session_keys_[tmsi] = {}; // key lives in the DlFlow after HO
        }
        (void)session_k;
    }
    void release_ue(uint32_t tmsi) {
        ue_contexts_.erase(tmsi);
        session_keys_.erase(tmsi);
    }

    const UeContext* find_ue(uint32_t tmsi) const;

    // M16: reverse lookup for UE-to-UE forwarding (IMSI acts as the "phone
    // number"). nullptr when no registered context carries this IMSI.
    const UeContext* find_ue_by_imsi(const std::string& imsi) const;

    // M12: session key per authenticated TMSI (nullptr if unknown).
    const std::array<uint8_t, crypto::kKey256Size>* session_key(
        uint32_t tmsi) const {
        auto it = session_keys_.find(tmsi);
        return it == session_keys_.end() ? nullptr : &it->second;
    }

private:
    // M21: build a fresh AKA vector for ctx.imsi (advancing its SQN) and
    // send AUTH_REQUEST. Used for the initial challenge and AUTS retries.
    void send_challenge(const UeContext& ctx);

    SendCallback send_cb_;
    std::unordered_map<uint32_t, UeContext> ue_contexts_;
    std::unordered_map<uint32_t, std::array<uint8_t, crypto::kKey256Size>> session_keys_;
    std::unordered_map<uint32_t, UeContext> pending_auth_;
    std::unordered_map<std::string, std::array<uint8_t, crypto::kKey256Size>> keys_;
    std::unordered_map<std::string, uint64_t> sqn_; // M21: per-subscriber SQN
    RandFn rand_fn_;
    uint32_t next_tmsi_ = 0x00010001;
};

}

#endif

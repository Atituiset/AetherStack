#ifndef AETHER_CN_AMF_H
#define AETHER_CN_AMF_H

// M15: AMF — the control-plane core entity. Owns what NasBs used to own
// inside BsNode (subscriber HSS, authentication, TMSI allocation, session
// keys) and speaks NG-like messages over a CnLink. The gNB becomes a pure
// radio node: NAS PDUs pass through it opaquely.

#include "cn/cn_link.h"
#include "common/crypto.h"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cn {

class Amf {
public:
    explicit Amf(CnLink& link) : link_(link) {
        link_.set_handler([this](const CnMessage& m) { handle(m); });
    }

    // Operator HSS: register a subscriber's master key by IMSI.
    void add_subscriber(const std::string& imsi,
                        const std::array<uint8_t, crypto::kKey256Size>& k) {
        keys_[imsi] = k;
    }

    size_t registered_count() const { return ue_contexts_.size(); }
    bool is_ue_registered(uint32_t tmsi) const {
        return ue_contexts_.count(tmsi) != 0;
    }

    struct UeContext {
        std::string imsi;
        uint32_t tmsi = 0;
        bool registered = false;
        uint16_t serving_rnti = 0;   // C-RNTI at the serving gNB
        uint16_t serving_cell = 0;   // cell id of the serving gNB
    };

    const UeContext* find_ue(uint32_t tmsi) const {
        auto it = ue_contexts_.find(tmsi);
        return it == ue_contexts_.end() ? nullptr : &it->second;
    }

    void release_ue(uint32_t tmsi);

private:
    void handle(const CnMessage& msg);
    void send(MsgType t, std::vector<uint8_t> value) {
        CnMessage m;
        m.msg_type = t;
        m.value = std::move(value);
        link_.send(m);
    }

    CnLink& link_;
    std::unordered_map<uint32_t, UeContext> ue_contexts_;
    std::unordered_map<uint32_t, UeContext> pending_auth_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> pending_rnti_; // auth ctx
    std::unordered_map<std::string, std::array<uint8_t, crypto::kKey256Size>> keys_;
    std::unordered_map<uint32_t, std::array<uint8_t, crypto::kKey256Size>> session_keys_;
    uint32_t next_tmsi_ = 0x00010001;
};

} // namespace cn

#endif

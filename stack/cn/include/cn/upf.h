#ifndef AETHER_CN_UPF_H
#define AETHER_CN_UPF_H

// M15: UPF — the user-plane anchor. Every UE's data path terminates here:
// gNBs forward decrypted uplink SDUs up, and downlink SDUs come back routed
// to whichever gNB currently serves the UE. Handover becomes a PATH_SWITCH
// table update — the anchor (and the application flow) never moves.

#include "cn/cn_link.h"
#include <cstdint>
#include <unordered_map>

namespace cn {

class Upf {
public:
    explicit Upf(CnLink& link) : link_(link) {
        link_.set_handler([this](const CnMessage& m) { handle(m); });
    }

    // Downlink injection point: the "internet" side of the anchor.
    using DlSink = std::function<void(uint32_t tmsi, const std::vector<uint8_t>& pdu)>;
    void set_dl_sink(DlSink cb) { dl_sink_ = std::move(cb); }

    // Uplink sink for tests / demo drivers (what used to be the gNB echo).
    using UlSink = std::function<void(uint32_t tmsi, const std::vector<uint8_t>& pdu)>;
    void set_ul_sink(UlSink cb) { ul_sink_ = std::move(cb); }

    // Push a downlink PDU towards a UE (from the network side).
    void send_downlink(uint32_t tmsi, const std::vector<uint8_t>& pdu);

    size_t session_count() const { return routes_.size(); }

private:
    struct Route {
        uint16_t rnti = 0;      // C-RNTI at the serving gNB
        uint16_t gnb_cell = 0;  // serving cell id
    };
    void handle(const CnMessage& msg);
    void send(MsgType t, std::vector<uint8_t> value) {
        CnMessage m;
        m.msg_type = t;
        m.value = std::move(value);
        link_.send(m);
    }

    CnLink& link_;
    std::unordered_map<uint32_t, Route> routes_;
    DlSink dl_sink_;
    UlSink ul_sink_;
};

} // namespace cn

#endif

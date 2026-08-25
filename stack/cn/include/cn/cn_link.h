#ifndef AETHER_CN_CN_LINK_H
#define AETHER_CN_CN_LINK_H

// M15: transport between the gNB and the core-network entities. One abstract
// link per direction (to AMF, to UPF) so BsNode stays agnostic; two concrete
// carriers:
//
//   * InMemoryCnLink — same-process delivery via std::function, used by unit
//     tests and by BsNode's default wiring (behaviour identical to M14).
//   * UdpCnLink      — datagrams over transport::UdpTransport for the
//     standalone amfd / upfd processes.

#include "cn/cn_messages.h"
#include <functional>

namespace cn {

using CnHandler = std::function<void(const CnMessage&)>;

class CnLink {
public:
    virtual ~CnLink() = default;

    // Register who receives what this link delivers.
    virtual void set_handler(CnHandler cb) = 0;
    // Send a message towards the far entity.
    virtual void send(const CnMessage& msg) = 0;
};

// Loopback carrier: send() invokes the peer handler synchronously. Supports
// multiple peers (e.g. one AMF/UPF serving several gNBs): every send is
// delivered to all connected peers. Wiring is explicit at composition time.
class InMemoryCnLink : public CnLink {
public:
    void set_handler(CnHandler cb) override { handler_ = std::move(cb); }

    // Wire this link to a peer: messages I send are delivered there.
    void connect_to(InMemoryCnLink* peer) {
        if (!peer) return;
        peers_.push_back(peer);
        peer->peers_.push_back(this);
    }

    void send(const CnMessage& msg) override {
        for (auto* p : peers_) {
            if (p->handler_) p->handler_(msg);
        }
    }

private:
    std::vector<InMemoryCnLink*> peers_;
    CnHandler handler_;
};

} // namespace cn

#endif

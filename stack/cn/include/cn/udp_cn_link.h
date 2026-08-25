#ifndef AETHER_CN_UDP_CN_LINK_H
#define AETHER_CN_UDP_CN_LINK_H

// M15: UDP carrier for CnLink — connects a gNB process to standalone
// amfd / upfd processes over datagram sockets. Each link owns one bound
// socket; the far entity's address is set explicitly. receive() drains one
// pending datagram (call from the process main loop alongside tick()).

#include "cn/cn_link.h"
#include "common/udp_transport.h"
#include <functional>
#include <string>

namespace cn {

class UdpCnLink : public CnLink {
public:
    bool bind(uint16_t local_port) {
        return sock_.bind("0.0.0.0", local_port);
    }
    void set_remote(const std::string& addr, uint16_t port) {
        sock_.set_dest(addr, port);
        has_remote_ = true;
    }

    void set_handler(CnHandler cb) override { handler_ = std::move(cb); }

    void send(const CnMessage& msg) override {
        auto data = msg.encode();
        if (has_remote_) {
            sock_.send(data);
        } else {
            sock_.reply(data.data(), data.size()); // server mode
        }
    }

    // Non-blocking poll: decodes and dispatches at most one datagram.
    // Returns true when a message was handled.
    bool receive(int timeout_ms = 0) {
        uint8_t buf[2048];
        std::string src_addr;
        uint16_t src_port = 0;
        int n = sock_.recv_from(buf, sizeof(buf), &src_addr, &src_port,
                                timeout_ms);
        if (n <= 0) return false;
        if (!handler_) return true; // drained but nobody listening
        handler_(CnMessage::decode(std::vector<uint8_t>(buf, buf + n)));
        return true;
    }

private:
    transport::UdpTransport sock_;
    CnHandler handler_;
    bool has_remote_ = false;
};

} // namespace cn

#endif

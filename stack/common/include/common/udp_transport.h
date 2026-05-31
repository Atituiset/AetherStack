#ifndef AETHER_COMMON_UDP_TRANSPORT_H
#define AETHER_COMMON_UDP_TRANSPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace transport {

class UdpTransport {
public:
    UdpTransport();
    ~UdpTransport();

    bool bind(const std::string& local_addr, uint16_t local_port);
    bool set_dest(const std::string& dest_addr, uint16_t dest_port);
    bool send(const uint8_t* data, size_t len);
    bool send(const std::vector<uint8_t>& data);
    int recv(uint8_t* buf, size_t buf_len, int timeout_ms = 1000);

    int fd() const { return sock_fd_; }

private:
    int sock_fd_ = -1;
    struct DestAddr {
        std::string addr;
        uint16_t port = 0;
    } dest_;
};

}

#endif

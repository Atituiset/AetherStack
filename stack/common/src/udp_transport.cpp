#include "common/udp_transport.h"
#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace transport {

UdpTransport::UdpTransport() {
    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
}

UdpTransport::~UdpTransport() {
    if (sock_fd_ >= 0) close(sock_fd_);
}

bool UdpTransport::bind(const std::string& local_addr, uint16_t local_port) {
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local_port);
    if (local_addr.empty() || local_addr == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, local_addr.c_str(), &addr.sin_addr);
    }
    return ::bind(sock_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
}

bool UdpTransport::set_dest(const std::string& dest_addr, uint16_t dest_port) {
    dest_ = {dest_addr, dest_port};
    return true;
}

bool UdpTransport::send(const uint8_t* data, size_t len) {
    if (sock_fd_ < 0 || dest_.port == 0) return false;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dest_.port);
    inet_pton(AF_INET, dest_.addr.c_str(), &addr.sin_addr);
    ssize_t ret = sendto(sock_fd_, data, len, 0,
                          reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return ret == static_cast<ssize_t>(len);
}

bool UdpTransport::send(const std::vector<uint8_t>& data) {
    return send(data.data(), data.size());
}

int UdpTransport::recv(uint8_t* buf, size_t buf_len, int timeout_ms) {
    int flags = 0;
    if (timeout_ms <= 0) {
        // A zero SO_RCVTIMEO means "block forever" on Linux; make 0 an
        // explicit non-blocking poll instead.
        flags = MSG_DONTWAIT;
    } else {
        struct timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    return recvfrom(sock_fd_, buf, buf_len, flags, nullptr, nullptr);
}

}

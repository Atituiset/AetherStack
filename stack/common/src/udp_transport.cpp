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
    // M16.1: media-rate bursts are ~20 KB each; the default ~200 KB kernel
    // receive buffer holds only ~10 of them, so a brief processing stall
    // silently kernel-drops datagrams far above the intended channel loss
    // rate and tips HARQ into a retransmission storm. Size the buffer for
    // a real backlog (the kernel doubles and caps at rmem_max).
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
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

// M15: capture the sender so a server (AMF/UPF) can reply without having
// been told the client's address up front.
int UdpTransport::recv_from(uint8_t* buf, size_t buf_len,
                            std::string* sender_addr, uint16_t* sender_port,
                            int timeout_ms) {
    struct sockaddr_in src{};
    socklen_t src_len = sizeof(src);
    int flags = 0;
    if (timeout_ms <= 0) {
        flags = MSG_DONTWAIT;
    } else {
        struct timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    int n = recvfrom(sock_fd_, buf, buf_len, flags,
                     reinterpret_cast<struct sockaddr*>(&src), &src_len);
    if (n > 0) {
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
        if (sender_addr) *sender_addr = ip;
        if (sender_port) *sender_port = ntohs(src.sin_port);
        last_sender_ = {ip, ntohs(src.sin_port)};
    }
    return n;
}

bool UdpTransport::reply(const uint8_t* data, size_t len) {
    if (sock_fd_ < 0 || last_sender_.port == 0) return false;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(last_sender_.port);
    inet_pton(AF_INET, last_sender_.addr.c_str(), &addr.sin_addr);
    ssize_t ret = sendto(sock_fd_, data, len, 0,
                          reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return ret == static_cast<ssize_t>(len);
}

}

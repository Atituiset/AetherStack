// M6.5 T7: thin process shell. All protocol logic lives in core::UeNode;
// this main only wires the UDP radio, drives the timer tick and forwards
// stdin commands (attach / detach / send <text> / status / quit).
#include "common/logger.h"
#include "common/udp_transport.h"
#include "core/ue_node.h"
#include "phy/phy_io.h"
#include "phy/ofdm.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

namespace {

uint32_t monotonic_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

bool stdin_has_line() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    timeval tv{0, 0};
    return select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string log_host = "127.0.0.1";
    uint16_t log_port = 9999;
    uint16_t local_phy_port = 10001;
    std::string bs_addr = "127.0.0.1";
    uint16_t bs_phy_port = 20002;
    int n_fft = 64;
    int cp_len = 16;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-host" && i + 1 < argc) log_host = argv[++i];
        else if (arg == "--log-port" && i + 1 < argc) log_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--local-phy-port" && i + 1 < argc) local_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--bs-phy-addr" && i + 1 < argc) bs_addr = argv[++i];
        else if (arg == "--bs-phy-port" && i + 1 < argc) bs_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }

    logging::init("UE", log_host, log_port);
    LOG_INFO("PROCESS_START", {{"msg", "UE protocol stack started"}});
    LOG_INFO("PHY_CONFIG", {{"n_fft", std::to_string(n_fft)}, {"cp_len", std::to_string(cp_len)}});

    transport::UdpTransport phy_sock;
    if (!phy_sock.bind("0.0.0.0", local_phy_port)) {
        LOG_ERROR("PHY_BIND_FAIL", {{"port", std::to_string(local_phy_port)}});
        return 1;
    }
    phy_sock.set_dest(bs_addr, bs_phy_port);
    LOG_INFO("PHY_UDP_READY", {{"local_port", std::to_string(local_phy_port)},
                                {"dest", bs_addr + ":" + std::to_string(bs_phy_port)}});

    core::UeNode ue;
    ue.set_air_send([&](const std::vector<uint8_t>& bits) {
        auto iq = phy::phy_tx(bits, n_fft, cp_len);
        phy_sock.send(phy::iq_to_bytes(iq));
    });

    LOG_INFO("UE_CMD_HINT", {{"cmd", "attach | detach | send <text> | status | quit"}});

    while (true) {
        // Radio downlink -> protocol stack
        uint8_t rx_buf[65536];
        int rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 10);
        if (rx_len > 0) {
            auto iq = phy::bytes_to_iq(rx_buf, rx_len);
            if (!iq.empty()) {
                ue.on_air_bits(phy::phy_rx_auto(iq, n_fft, cp_len));
            }
        }

        ue.tick(monotonic_ms());

        if (stdin_has_line()) {
            std::string line;
            std::getline(std::cin, line);
            if (line == "attach") {
                ue.attach();
            } else if (line == "detach") {
                ue.detach();
            } else if (line.rfind("send ", 0) == 0) {
                std::vector<uint8_t> payload(line.begin() + 5, line.end());
                ue.send_app_data(payload);
            } else if (line == "status") {
                LOG_INFO("UE_STATUS", {{"mac", std::to_string(static_cast<int>(ue.mac_state()))},
                                        {"rrc", std::to_string(static_cast<int>(ue.rrc_state()))},
                                        {"nas", std::to_string(static_cast<int>(ue.nas_state()))},
                                        {"c_rnti", std::to_string(ue.crnti())},
                                        {"sib", ue.has_system_info() ? "1" : "0"},
                                        {"app_rx", std::to_string(ue.app_rx_count())}});
            } else if (line == "quit") {
                break;
            }
        }
    }

    LOG_INFO("PROCESS_EXIT", {{"module", "UE"}});
    return 0;
}

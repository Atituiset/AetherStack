// M6.5 T7/T9: thin process shell. All protocol logic lives in core::UeNode;
// this main only wires the UDP radio, drives the timer tick and forwards
// commands from stdin or the UDP command port (attach / detach / send <text> /
// status / quit).
#include "common/logger.h"
#include "common/udp_transport.h"
#include "core/ue_node.h"
#include "phy/phy_io.h"
#include "phy/ofdm.h"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

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

// One command dispatch shared by stdin and the UDP command channel.
bool handle_command(const std::string& line, core::UeNode& ue) {
    if (line == "attach") {
        ue.attach();
    } else if (line == "detach") {
        ue.detach();
    } else if (line.rfind("send ", 0) == 0) {
        std::vector<uint8_t> payload(line.begin() + 5, line.end());
        ue.send_app_data(payload);
    } else if (line == "status") {
        LOG_INFO(ev::UE_STATUS, {{"mac", std::to_string(static_cast<int>(ue.mac_state()))},
                                  {"rrc", std::to_string(static_cast<int>(ue.rrc_state()))},
                                  {"nas", std::to_string(static_cast<int>(ue.nas_state()))},
                                  {"c_rnti", std::to_string(ue.crnti())},
                                  {"sib", ue.has_system_info() ? "1" : "0"},
                                  {"app_rx", std::to_string(ue.app_rx_count())}});
    } else if (line == "quit") {
        return false; // stop the main loop
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string log_host = "127.0.0.1";
    uint16_t log_port = 9999;
    uint16_t local_phy_port = 10001;
    uint16_t cmd_port = 10101;
    std::string bs_addr = "127.0.0.1";
    uint16_t bs_phy_port = 20002;
    int n_fft = 64;
    int cp_len = 16;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-host" && i + 1 < argc) log_host = argv[++i];
        else if (arg == "--log-port" && i + 1 < argc) log_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--local-phy-port" && i + 1 < argc) local_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--cmd-port" && i + 1 < argc) cmd_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--bs-phy-addr" && i + 1 < argc) bs_addr = argv[++i];
        else if (arg == "--bs-phy-port" && i + 1 < argc) bs_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }

    logging::init("UE", log_host, log_port);
    LOG_INFO(ev::PROCESS_START, {{"msg", "UE protocol stack started"}});
    LOG_INFO(ev::PHY_CONFIG, {{"n_fft", std::to_string(n_fft)}, {"cp_len", std::to_string(cp_len)}});

    transport::UdpTransport phy_sock;
    if (!phy_sock.bind("0.0.0.0", local_phy_port)) {
        LOG_ERROR(ev::PHY_BIND_FAIL, {{"port", std::to_string(local_phy_port)}});
        return 1;
    }
    phy_sock.set_dest(bs_addr, bs_phy_port);
    LOG_INFO(ev::PHY_UDP_READY, {{"local_port", std::to_string(local_phy_port)},
                                  {"dest", bs_addr + ":" + std::to_string(bs_phy_port)}});

    transport::UdpTransport cmd_sock;
    const bool cmd_enabled = cmd_sock.bind("0.0.0.0", cmd_port);
    if (cmd_enabled) {
        LOG_INFO(ev::PHY_UDP_READY, {{"local_port", std::to_string(cmd_port)},
                                      {"dest", "command channel"}});
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    core::UeNode ue;
    ue.set_air_send([&](const std::vector<uint8_t>& bits) {
        auto iq = phy::phy_tx(bits, n_fft, cp_len);
        phy_sock.send(phy::iq_to_bytes(iq));
    });

    LOG_INFO(ev::UE_CMD_HINT, {{"cmd", "attach | detach | send <text> | status | quit"}});

    uint32_t now_ms = monotonic_ms();
    ue.tick(now_ms);
    uint32_t next_heartbeat = now_ms + 5000;
    bool stdin_eof = false;

    while (!g_stop) {
        // Radio downlink -> protocol stack
        uint8_t rx_buf[65536];
        int rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 10);
        if (rx_len > 0) {
            auto iq = phy::bytes_to_iq(rx_buf, rx_len);
            if (!iq.empty()) {
                ue.on_air_bits(phy::phy_rx_auto(iq, n_fft, cp_len));
            }
        }

        // UDP command channel
        if (cmd_enabled) {
            int cmd_len = cmd_sock.recv(rx_buf, sizeof(rx_buf) - 1, 0);
            if (cmd_len > 0) {
                rx_buf[cmd_len] = '\0';
                std::string line(reinterpret_cast<char*>(rx_buf));
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                if (!handle_command(line, ue)) g_stop = 1;
            }
        }

        // stdin command channel: EOF (e.g. backgrounded with a closed pipe)
        // disables this channel only -- the process keeps running.
        if (!stdin_eof && stdin_has_line()) {
            std::string line;
            if (std::getline(std::cin, line)) {
                if (!handle_command(line, ue)) break;
            } else {
                stdin_eof = true;
                LOG_INFO(ev::UE_CMD_HINT, {{"cmd", "stdin closed; use UDP command port"}});
            }
        }

        now_ms = monotonic_ms();
        ue.tick(now_ms);
        if (static_cast<int32_t>(now_ms - next_heartbeat) >= 0) {
            next_heartbeat = now_ms + 5000;
            LOG_INFO(ev::HEARTBEAT, {{"c_rnti", std::to_string(ue.crnti())},
                                      {"registered", ue.registered() ? "1" : "0"}});
        }
    }

    LOG_INFO(ev::PROCESS_EXIT, {});
    return 0;
}

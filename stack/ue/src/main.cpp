// M6.5 T7/T9: thin process shell. All protocol logic lives in core::UeNode;
// this main only wires the UDP radio, drives the timer tick and forwards
// commands from stdin or the UDP command port (attach / detach / send <text> /
// status / quit).
#include "common/logger.h"
#include "common/udp_transport.h"
#include "core/ue_node.h"
#include "phy/frame.h"
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
    } else if (line == "traffic" || line == "traffic on") {
        ue.start_traffic();
    } else if (line == "traffic off") {
        ue.stop_traffic();
        LOG_INFO(ev::UE_STATUS, {{"mac", std::to_string(static_cast<int>(ue.mac_state()))},
                                  {"rrc", std::to_string(static_cast<int>(ue.rrc_state()))},
                                  {"nas", std::to_string(static_cast<int>(ue.nas_state()))},
                                  {"c_rnti", std::to_string(ue.crnti())},
                                  {"sib", ue.has_system_info() ? "1" : "0"},
                                  {"app_rx", std::to_string(ue.app_rx_count())}});
    } else if (line.rfind("send ", 0) == 0) {
        std::vector<uint8_t> payload(line.begin() + 5, line.end());
        ue.send_app_data(payload);
    } else if (line.rfind("msg ", 0) == 0) {
        // msg <peer-imsi> <text> — one-shot text to another UE.
        const std::string rest = line.substr(4);
        const auto sp = rest.find(' ');
        if (sp != std::string::npos && sp + 1 < rest.size()) {
            ue.send_msg(rest.substr(0, sp), rest.substr(sp + 1));
        }
    } else if (line.rfind("call ", 0) == 0) {
        const std::string arg = line.substr(5);
        if (arg == "end") ue.end_call(app::MediaKind::VOICE);
        else ue.start_call(app::MediaKind::VOICE, arg);
    } else if (line.rfind("video ", 0) == 0) {
        const std::string arg = line.substr(6);
        if (arg == "end") ue.end_call(app::MediaKind::VIDEO);
        else ue.start_call(app::MediaKind::VIDEO, arg);
    } else if (line.rfind("conf", 0) == 0) {
        // M18: conf <imsiB> <imsiC> — start a 3-party conference (BS audio
        // bridge); conf end — host tears the whole conference down. A
        // participant leaves with "call end".
        if (line == "conf end") {
            ue.end_conf();
        } else if (line.rfind("conf ", 0) == 0) {
            const std::string rest = line.substr(5);
            const auto sp = rest.find(' ');
            if (sp != std::string::npos && sp + 1 < rest.size()) {
                ue.start_conf(rest.substr(0, sp), rest.substr(sp + 1));
            }
        }
    } else if (line == "answer") {
        ue.answer();   // accept a ringing incoming call (SIP-lite 200 OK)
    } else if (line == "sleep") {
        ue.sleep();    // M20: ask the network for RRC_INACTIVE (suspend)
    } else if (line == "wake") {
        ue.wake();     // M20: fast resume from RRC_INACTIVE
    } else if (line == "decline") {
        ue.decline();  // reject a ringing incoming call (SIP-lite 603)
    } else if (line.rfind("autoanswer", 0) == 0) {
        // autoanswer <ms>|off — unattended UEs pick up after <ms> (default 4s)
        if (line == "autoanswer off") {
            ue.set_autoanswer(0);
        } else if (line.size() > 11 &&
                   line.find_first_not_of("0123456789", 11) == std::string::npos) {
            ue.set_autoanswer(static_cast<uint32_t>(std::stoul(line.substr(11))));
        }
    } else if (line == "stats") {
        ue.emit_traffic_stats();
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
    int ue_id = 1;
    std::string imsi = "460011234567890";
    int n_fft = 64;
    int cp_len = 16;
    std::string usim_key_hex; // M21: USIM master key (hex) for AKA

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-host" && i + 1 < argc) log_host = argv[++i];
        else if (arg == "--log-port" && i + 1 < argc) log_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--local-phy-port" && i + 1 < argc) local_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--cmd-port" && i + 1 < argc) cmd_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--bs-phy-addr" && i + 1 < argc) bs_addr = argv[++i];
        else if (arg == "--bs-phy-port" && i + 1 < argc) bs_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--ue-id" && i + 1 < argc) ue_id = std::stoi(argv[++i]);
        else if (arg == "--imsi" && i + 1 < argc) imsi = argv[++i];
        else if (arg == "--usim-key" && i + 1 < argc) usim_key_hex = argv[++i];
    }

    logging::init("UE", log_host, log_port, "ue" + std::to_string(ue_id));
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

    core::UeNodeConfig ue_cfg;
    ue_cfg.imsi = imsi;
    // Shared-medium RACH: distinct preamble per UE so simultaneous attaches
    // get distinct RA-RNTI contexts (real UEs draw preambles at random).
    ue_cfg.rach.preamble_index =
        static_cast<mac::PreambleIndex>((42 + (ue_id - 1)) & 0x3F);
    core::UeNode ue(ue_cfg);
    if (!usim_key_hex.empty()) {
        // M21: provisioned USIM — attach runs the AKA exchange.
        std::array<uint8_t, crypto::kKey256Size> k{};
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        for (size_t i = 0; i + 1 < usim_key_hex.size() && i / 2 < k.size();
             i += 2) {
            k[i / 2] = static_cast<uint8_t>(
                (nib(usim_key_hex[i]) << 4) | nib(usim_key_hex[i + 1]));
        }
        ue.nas().set_usim_key(k);
    }
    const phy::FrameTxConfig frame_cfg{ n_fft, cp_len, /*pci=*/0 };
    // M22: RX accepts any confirmed cell (wildcard pci); the TX preamble
    // carries the SERVING cell's pci (convention pci == cell_id - 1).
    phy::FrameTxConfig rx_cfg = frame_cfg;
    rx_cfg.pci = -1;
    ue.set_air_send([&](const std::vector<uint8_t>& bits) {
        // Preamble (PSS/SSS/DMRS) + modulated data in one burst. M19: UL
        // stays QPSK (link adaptation is DL-only); the TX power loop
        // scales the whole burst, preamble included, like a real PA.
        phy::FrameTxConfig tx_cfg = frame_cfg;
        if (ue.serving_cell() != 0) {
            tx_cfg.pci = static_cast<int>(ue.serving_cell()) - 1;
        }
        std::vector<std::complex<float>> iq = phy::phy_preamble_burst(tx_cfg);
        auto data_iq = phy::phy_tx_data(bits, frame_cfg);
        iq.insert(iq.end(), data_iq.begin(), data_iq.end());
        const float g = static_cast<float>(ue.tx_gain());
        if (g != 1.0f) {
            for (auto& s : iq) s *= g;
        }
        phy_sock.send(phy::iq_to_bytes(iq));
    });

    LOG_INFO(ev::UE_CMD_HINT, {{"cmd", "attach | detach | send <text> | msg <imsi> <text> | call <imsi> | video <imsi> | conf <imsiB> <imsiC> | conf end | sleep | wake | answer | decline | call end | video end | autoanswer <ms>|off | traffic | stats | status | quit"}});

    uint32_t now_ms = monotonic_ms();
    ue.tick(now_ms);
    uint32_t next_heartbeat = now_ms + 5000;
    bool stdin_eof = false;

    while (!g_stop) {
        // Radio downlink -> protocol stack. First datagram blocks up to
        // 10ms; then drain any backlog non-blocking so a busy downlink
        // cannot pile up in the (small) kernel UDP buffer and get
        // silently dropped (M16.1).
        uint8_t rx_buf[65536];
        int rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 10);
        while (rx_len > 0) {
            auto iq = phy::bytes_to_iq(rx_buf, rx_len);
            if (!iq.empty()) {
                phy::FrameRxResult res;
                // M16.1: skip demodulating bursts addressed to the other UE
                // (downlink fan-out on the shared medium).
                auto bits = phy::phy_rx_frame(iq, rx_cfg, res, ue.crnti());
                if (res.synced && res.pci_confirmed && !bits.empty()) {
                    // M19: feed the link-metrics loop (CQI, TX power, RSRP).
                    double pwr = 1e-12;
                    for (const auto& s : iq) pwr += std::norm(s);
                    const float pwr_dbm = static_cast<float>(
                        10.0 * std::log10(pwr / iq.size() + 1e-12));
                    ue.on_air_bits_with_metrics(bits, res.snr_db, pwr_dbm);
                }
            }
            rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 0);
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

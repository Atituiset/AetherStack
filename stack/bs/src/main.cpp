// M6.5 T7/T9: thin process shell. All protocol logic lives in core::BsNode;
// this main only wires the UDP radio, drives the timer tick (SIB broadcast),
// answers the UDP command port (status/quit) and emits heartbeats.
#include "cn/udp_cn_link.h"
#include "common/logger.h"
#include "common/udp_transport.h"
#include "core/bs_node.h"
#include "phy/frame.h"
#include "phy/phy_io.h"
#include "phy/ofdm.h"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <string>
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

} // namespace

int main(int argc, char* argv[]) {
    std::string log_host = "127.0.0.1";
    uint16_t log_port = 9999;
    uint16_t local_phy_port = 20002;
    uint16_t cmd_port = 10102;
    std::string ue_addr = "127.0.0.1";
    uint16_t ue_phy_port = 10001;
    int n_fft = 64;
    int cp_len = 16;
    // M15: external core-network endpoints (empty = embedded legacy core).
    std::string amf_addr;
    uint16_t amf_port = 0;
    uint16_t ng_local_port = 20110;
    std::string upf_addr;
    uint16_t upf_port = 0;
    uint16_t gu_local_port = 20120;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-host" && i + 1 < argc) log_host = argv[++i];
        else if (arg == "--log-port" && i + 1 < argc) log_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--local-phy-port" && i + 1 < argc) local_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--cmd-port" && i + 1 < argc) cmd_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--ue-phy-addr" && i + 1 < argc) ue_addr = argv[++i];
        else if (arg == "--ue-phy-port" && i + 1 < argc) ue_phy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--amf-addr" && i + 1 < argc) amf_addr = argv[++i];
        else if (arg == "--amf-port" && i + 1 < argc) amf_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--ng-local-port" && i + 1 < argc) ng_local_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--upf-addr" && i + 1 < argc) upf_addr = argv[++i];
        else if (arg == "--upf-port" && i + 1 < argc) upf_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--gu-local-port" && i + 1 < argc) gu_local_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }

    logging::init("BS", log_host, log_port);
    LOG_INFO(ev::PROCESS_START, {{"msg", "BS protocol stack started"}});
    LOG_INFO(ev::PHY_CONFIG, {{"n_fft", std::to_string(n_fft)}, {"cp_len", std::to_string(cp_len)}});

    transport::UdpTransport phy_sock;
    if (!phy_sock.bind("0.0.0.0", local_phy_port)) {
        LOG_ERROR(ev::PHY_BIND_FAIL, {{"port", std::to_string(local_phy_port)}});
        return 1;
    }
    phy_sock.set_dest(ue_addr, ue_phy_port);
    LOG_INFO(ev::PHY_UDP_READY, {{"local_port", std::to_string(local_phy_port)},
                                  {"dest", ue_addr + ":" + std::to_string(ue_phy_port)}});

    transport::UdpTransport cmd_sock;
    const bool cmd_enabled = cmd_sock.bind("0.0.0.0", cmd_port);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    core::BsNode bs;

    // M15: optional split-core wiring. Both links must be given together.
    cn::UdpCnLink ng_link, gu_link;
    if (!amf_addr.empty() && amf_port != 0) {
        if (!ng_link.bind(ng_local_port)) {
            LOG_ERROR(ev::PHY_BIND_FAIL, {{"port", std::to_string(ng_local_port)}});
            return 1;
        }
        ng_link.set_remote(amf_addr, amf_port);
        bool have_upf = !upf_addr.empty() && upf_port != 0;
        if (have_upf) {
            if (!gu_link.bind(gu_local_port)) {
                LOG_ERROR(ev::PHY_BIND_FAIL, {{"port", std::to_string(gu_local_port)}});
                return 1;
            }
            gu_link.set_remote(upf_addr, upf_port);
        }
        core::BsNode::CnEndpoints ep;
        ep.amf = &ng_link;
        ep.upf = have_upf ? &gu_link : nullptr;
        ep.gnb_cell = 1;
        bs.attach_core(ep);
        LOG_INFO(ev::NG_SETUP_RX, {{"mode", "external-amf"},
                                    {"amf", amf_addr + ":" + std::to_string(amf_port)}});
    }

    const phy::FrameTxConfig frame_cfg{ n_fft, cp_len, /*pci=*/0 };
    bs.set_air_send([&](const std::vector<uint8_t>& bits) {
        std::vector<std::complex<float>> iq = phy::phy_preamble_burst(frame_cfg);
        auto data_iq = phy::phy_tx_data(bits, frame_cfg);
        iq.insert(iq.end(), data_iq.begin(), data_iq.end());
        phy_sock.send(phy::iq_to_bytes(iq));
    });

    uint32_t now_ms = monotonic_ms();
    bs.tick(now_ms);
    bs.start_broadcast();
    LOG_INFO(ev::BS_SIB_BROADCAST_ON, {{"period_ms", "200"}});

    uint32_t next_heartbeat = now_ms + 5000;

    while (!g_stop) {
        uint8_t rx_buf[65536];
        int rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 10);
        if (rx_len > 0) {
            auto iq = phy::bytes_to_iq(rx_buf, rx_len);
            if (!iq.empty()) {
                phy::FrameRxResult res;
                auto bits = phy::phy_rx_frame(iq, frame_cfg, res);
                if (res.synced && res.pci_confirmed && !bits.empty()) {
                    bs.on_air_bits(bits);
                }
            }
        }

        if (cmd_enabled) {
            int cmd_len = cmd_sock.recv(rx_buf, sizeof(rx_buf) - 1, 0);
            if (cmd_len > 0) {
                rx_buf[cmd_len] = '\0';
                std::string line(reinterpret_cast<char*>(rx_buf));
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                if (line == "status") {
                    LOG_INFO(ev::BS_STATUS, {{"registered_ues",
                                              std::to_string(bs.registered_ue_count())}});
                } else if (line == "quit") {
                    g_stop = 1;
                }
            }
        }

        // M15: drain NG/GTP-like datagrams from the external core.
        if (!amf_addr.empty()) {
            while (ng_link.receive(0)) {}
            while (gu_link.receive(0)) {}
        }

        now_ms = monotonic_ms();
        bs.tick(now_ms);
        if (static_cast<int32_t>(now_ms - next_heartbeat) >= 0) {
            next_heartbeat = now_ms + 5000;
            LOG_INFO(ev::HEARTBEAT, {{"registered_ues",
                                      std::to_string(bs.registered_ue_count())}});
        }
    }

    LOG_INFO(ev::PROCESS_EXIT, {});
    return 0;
}

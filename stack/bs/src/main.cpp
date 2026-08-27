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

// Parse up-to-32-byte hex key material ("a1b2..."; short input zero-pads).
std::array<uint8_t, crypto::kKey256Size> parse_key(const std::string& hex) {
    std::array<uint8_t, crypto::kKey256Size> k{};
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (size_t i = 0; i + 1 < hex.size() && i / 2 < k.size(); i += 2) {
        k[i / 2] = static_cast<uint8_t>((nib(hex[i]) << 4) | nib(hex[i + 1]));
    }
    return k;
}

} // namespace

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
    uint32_t inactive_ms = 0; // M20: 0 = never suspend (default)
    // M22: cell identity + Xn link to the peer gNB (dual-BS mobility).
    uint16_t cell_id = 1;
    int pci = 0;
    uint16_t crnti_base = 0x0001;
    uint16_t xn_local_port = 0, xn_peer_port = 0;
    std::string xn_peer_addr = "127.0.0.1";
    // M21: HSS provisioning on the command line: --subscriber imsi:hexkey
    // (repeatable). UEs without an entry attach via legacy open access.
    std::vector<std::pair<std::string, std::array<uint8_t, crypto::kKey256Size>>> subscribers;

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
        else if (arg == "--inactive-ms" && i + 1 < argc) inactive_ms = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (arg == "--cell-id" && i + 1 < argc) cell_id = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--pci" && i + 1 < argc) pci = std::stoi(argv[++i]);
        else if (arg == "--crnti-base" && i + 1 < argc) crnti_base = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--xn-local" && i + 1 < argc) xn_local_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--xn-peer" && i + 1 < argc) xn_peer_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--subscriber" && i + 1 < argc) {
            std::string spec = argv[++i];
            const auto colon = spec.find(':');
            if (colon != std::string::npos) {
                subscribers.emplace_back(spec.substr(0, colon),
                                         parse_key(spec.substr(colon + 1)));
            }
        }
    }

    logging::init("BS", log_host, log_port,
                  "bs" + std::to_string(cell_id)); // M22: bs1/bs2 in logs
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

    core::BsNodeConfig bs_cfg;
    bs_cfg.inactive_ms = inactive_ms; // M20: RRC_INACTIVE suspend timer
    bs_cfg.cell_id = cell_id;         // M22
    bs_cfg.pci = static_cast<uint16_t>(pci);
    bs_cfg.crnti_base = crnti_base;
    core::BsNode bs(bs_cfg);

    // M22: optional Xn link to the peer gNB (single-BS mode: leave unset).
    cn::UdpCnLink xn_link;
    if (xn_local_port != 0 && xn_peer_port != 0) {
        if (!xn_link.bind(xn_local_port)) {
            LOG_ERROR(ev::PHY_BIND_FAIL, {{"port", std::to_string(xn_local_port)}});
            return 1;
        }
        xn_link.set_remote(xn_peer_addr, xn_peer_port);
        bs.attach_xn(&xn_link, cell_id == 1 ? 2 : 1);
        LOG_INFO(ev::NG_SETUP_RX, {{"mode", "xn-peer"},
                                   {"amf", xn_peer_addr + ":" + std::to_string(xn_peer_port)}});
    }
    for (const auto& [imsi, key] : subscribers) {
        bs.nas().add_subscriber(imsi, key); // M21: AKA for provisioned UEs
    }

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

    const phy::FrameTxConfig frame_cfg{ n_fft, cp_len, pci };
    // M19: MCS-carrying radio path — BsNode picks the DL MCS per flow from
    // CQI reports; control/broadcast bursts stay QPSK.
    bs.set_air_send_ex([&](const core::AirFrame& frame, int mcs,
                           const std::vector<uint8_t>& bits) {
        std::vector<std::complex<float>> iq = phy::phy_preamble_burst(frame_cfg);
        auto data_iq = phy::phy_tx_data(bits, frame_cfg,
                                        static_cast<phy::Mcs>(mcs));
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
        // First datagram blocks up to 10ms; then drain the backlog
        // non-blocking so bursts cannot pile up in the (small) kernel UDP
        // buffer and get silently dropped (M16.1).
        int rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 10);
        while (rx_len > 0) {
            auto iq = phy::bytes_to_iq(rx_buf, rx_len);
            if (!iq.empty()) {
                phy::FrameRxResult res;
                auto bits = phy::phy_rx_frame(iq, frame_cfg, res);
                if (res.synced && res.pci_confirmed && !bits.empty()) {
                    // M19: per-burst UL SNR feeds the TPC control loop.
                    double pwr = 1e-12;
                    for (const auto& s : iq) pwr += std::norm(s);
                    const float pwr_dbm = static_cast<float>(
                        10.0 * std::log10(pwr / iq.size() + 1e-12));
                    bs.on_air_bits_with_metrics(bits, res.snr_db, pwr_dbm);
                }
            }
            rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 0);
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
        // M22: drain Xn datagrams from the peer gNB.
        if (xn_local_port != 0) {
            while (xn_link.receive(0)) {}
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

#include "common/logger.h"
#include "common/udp_transport.h"
#include "phy/ofdm.h"
#include "phy/phy_io.h"
#include "phy/qpsk.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

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

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> bit_dist(0, 1);

    int frame_count = 0;
    const size_t bits_per_frame = 128;

    while (true) {
        std::vector<uint8_t> tx_bits(bits_per_frame);
        for (auto& b : tx_bits) b = bit_dist(rng);

        auto iq_samples = phy::phy_tx(tx_bits, n_fft, cp_len);
        auto pkt = phy::iq_to_bytes(iq_samples);
        phy_sock.send(pkt);

        LOG_INFO("PHY_TX", {{"frame", std::to_string(++frame_count)},
                             {"bits", std::to_string(bits_per_frame)},
                             {"iq_samples", std::to_string(iq_samples.size())}});

        uint8_t rx_buf[65536];
        int rx_len = phy_sock.recv(rx_buf, sizeof(rx_buf), 500);
        if (rx_len > 0) {
            auto rx_iq = phy::bytes_to_iq(rx_buf, rx_len);
            if (!rx_iq.empty()) {
                auto rx_bits = phy::phy_rx(rx_iq, bits_per_frame, n_fft, cp_len);
                LOG_INFO("PHY_RX", {{"frame", std::to_string(frame_count)},
                                     {"iq_samples", std::to_string(rx_iq.size())}});
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}

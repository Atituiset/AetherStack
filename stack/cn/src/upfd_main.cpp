// M15: standalone UPF process. Thin shell around cn::Upf — binds a GTP-U-like
// UDP port, drains datagrams, echoes uplink back as downlink (network-side
// ping anchor), emits heartbeats.
#include "common/logger.h"
#include "cn/upf.h"
#include "cn/udp_cn_link.h"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>

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
    uint16_t gu_port = 10120;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-host" && i + 1 < argc) log_host = argv[++i];
        else if (arg == "--log-port" && i + 1 < argc) log_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--gu-port" && i + 1 < argc) gu_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }

    logging::init("UPF", log_host, log_port);
    LOG_INFO(ev::PROCESS_START, {{"msg", "UPF user-plane anchor started"}});

    cn::UdpCnLink gu_link;
    if (!gu_link.bind(gu_port)) {
        LOG_ERROR(ev::PHY_BIND_FAIL, {{"port", std::to_string(gu_port)}});
        return 1;
    }
    LOG_INFO(ev::PHY_UDP_READY, {{"local_port", std::to_string(gu_port)}});

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    cn::Upf upf(gu_link);
    // Demo anchor behaviour: network side echoes every uplink PDU downlink.
    upf.set_ul_sink([&upf](uint32_t tmsi, const std::vector<uint8_t>& pdu) {
        upf.send_downlink(tmsi, pdu);
    });

    uint32_t next_hb = monotonic_ms() + 5000;
    while (!g_stop) {
        gu_link.receive(10);
        const uint32_t now = monotonic_ms();
        if (static_cast<int32_t>(now - next_hb) >= 0) {
            next_hb = now + 5000;
            LOG_INFO(ev::HEARTBEAT,
                     {{"sessions", std::to_string(upf.session_count())}});
        }
    }

    LOG_INFO(ev::PROCESS_EXIT, {});
    return 0;
}

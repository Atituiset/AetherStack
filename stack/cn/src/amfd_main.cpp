// M15: standalone AMF process. Thin shell around cn::Amf — binds an NG-like
// UDP port, drains datagrams in a loop, replies to the requesting gNB,
// emits heartbeats. Serves the most recent peer (single-cell demo model).
#include "common/logger.h"
#include "cn/amf.h"
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
    uint16_t ng_port = 10110;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-host" && i + 1 < argc) log_host = argv[++i];
        else if (arg == "--log-port" && i + 1 < argc) log_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--ng-port" && i + 1 < argc) ng_port = static_cast<uint16_t>(std::stoi(argv[++i]));
    }

    logging::init("AMF", log_host, log_port);
    LOG_INFO(ev::PROCESS_START, {{"msg", "AMF core started"}});

    cn::UdpCnLink ng_link;
    if (!ng_link.bind(ng_port)) {
        LOG_ERROR(ev::PHY_BIND_FAIL, {{"port", std::to_string(ng_port)}});
        return 1;
    }
    LOG_INFO(ev::PHY_UDP_READY, {{"local_port", std::to_string(ng_port)}});

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    cn::Amf amf(ng_link);

    uint32_t next_hb = monotonic_ms() + 5000;
    while (!g_stop) {
        ng_link.receive(10);
        const uint32_t now = monotonic_ms();
        if (static_cast<int32_t>(now - next_hb) >= 0) {
            next_hb = now + 5000;
            LOG_INFO(ev::HEARTBEAT,
                     {{"registered_ues", std::to_string(amf.registered_count())}});
        }
    }

    LOG_INFO(ev::PROCESS_EXIT, {});
    return 0;
}

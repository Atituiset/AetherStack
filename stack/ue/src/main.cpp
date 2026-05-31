#include "common/logger.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    std::string remote_host = "127.0.0.1";
    uint16_t remote_port = 9999;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-host" && i + 1 < argc) {
            remote_host = argv[++i];
        } else if (arg == "--log-port" && i + 1 < argc) {
            remote_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }

    logging::init("UE", remote_host, remote_port);
    LOG_INFO("PROCESS_START", {{"msg", "UE protocol stack started"}});

    // MVP placeholder: heartbeat loop until terminated
    int count = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        LOG_INFO("HEARTBEAT", {{"count", std::to_string(++count)}});
    }

    return 0;
}

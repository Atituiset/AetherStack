#include "common/logger.h"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

// POSIX sockets for UDP logging
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace logging {

namespace {

std::string g_module = "UNKNOWN";
std::string g_remote_host;
uint16_t g_remote_port = 0;
int g_udp_sock = -1;
sockaddr_in g_remote_addr{};
bool g_udp_enabled = false;
std::mutex g_mutex;

std::string escape_json(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

std::string iso8601_utc_now() {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()) % 1'000'000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setw(6) << std::setfill('0') << us.count() << 'Z';
    return ss.str();
}

std::string level_to_string(Level lvl) {
    switch (lvl) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO";
        case Level::WARN:  return "WARN";
        case Level::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

} // anonymous namespace

void init(const std::string& module_name,
          const std::string& remote_host,
          uint16_t remote_port) {
    std::lock_guard<std::mutex> lock(g_mutex);

    g_module = module_name;
    g_remote_host = remote_host;
    g_remote_port = remote_port;

    if (g_udp_sock >= 0) {
        close(g_udp_sock);
        g_udp_sock = -1;
    }
    g_udp_enabled = false;

    if (!remote_host.empty() && remote_port > 0) {
        g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (g_udp_sock < 0) {
            std::cerr << "[logger] failed to create UDP socket\n";
            return;
        }

        std::memset(&g_remote_addr, 0, sizeof(g_remote_addr));
        g_remote_addr.sin_family = AF_INET;
        g_remote_addr.sin_port = htons(remote_port);

        if (inet_pton(AF_INET, remote_host.c_str(), &g_remote_addr.sin_addr) <= 0) {
            std::cerr << "[logger] invalid remote host: " << remote_host << "\n";
            close(g_udp_sock);
            g_udp_sock = -1;
            return;
        }

        g_udp_enabled = true;
    }
}

void log(Level level,
         const std::string& event,
         const std::map<std::string, std::string>& fields) {
    std::lock_guard<std::mutex> lock(g_mutex);

    std::ostringstream ss;
    ss << "{"
       << "\"timestamp\":\"" << iso8601_utc_now() << "\""
       << ",\"module\":\"" << escape_json(g_module) << "\""
       << ",\"level\":\"" << level_to_string(level) << "\""
       << ",\"event\":\"" << escape_json(event) << "\""
       << ",\"fields\":{";

    bool first = true;
    for (const auto& [k, v] : fields) {
        if (!first) ss << ",";
        ss << "\"" << escape_json(k) << "\":\"" << escape_json(v) << "\"";
        first = false;
    }
    ss << "}}";

    std::string json = ss.str();
    std::cout << json << std::endl; // flush: stdout is line-delimited telemetry

    if (g_udp_enabled && g_udp_sock >= 0) {
        sendto(g_udp_sock, json.c_str(), json.size(), 0,
               reinterpret_cast<struct sockaddr*>(&g_remote_addr),
               sizeof(g_remote_addr));
    }
}

} // namespace logging

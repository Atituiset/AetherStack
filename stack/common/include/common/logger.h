#ifndef AETHER_COMMON_LOGGER_H
#define AETHER_COMMON_LOGGER_H

#include "common/events.h"
#include <cstdint>
#include <map>
#include <string>

namespace logging {

enum class Level {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

// Initialize logger for a module (e.g., "UE", "BS").
// If remote_host is non-empty and remote_port > 0, logs are also sent via UDP.
// node_name tags every record with a "node" field (e.g., "ue1", "bs") so a
// multi-node deployment can be told apart; empty defaults to the lowercased
// module name.
void init(const std::string& module_name,
          const std::string& remote_host = "",
          uint16_t remote_port = 0,
          const std::string& node_name = "");

// Core log function. `fields` is optional key-value context.
void log(Level level,
         const std::string& event,
         const std::map<std::string, std::string>& fields = {});

// Convenience macros
#define LOG_DEBUG(event, ...) ::logging::log(::logging::Level::DEBUG, event, ##__VA_ARGS__)
#define LOG_INFO(event, ...)  ::logging::log(::logging::Level::INFO,  event, ##__VA_ARGS__)
#define LOG_WARN(event, ...)  ::logging::log(::logging::Level::WARN,  event, ##__VA_ARGS__)
#define LOG_ERROR(event, ...) ::logging::log(::logging::Level::ERROR, event, ##__VA_ARGS__)

} // namespace logging

#endif // AETHER_COMMON_LOGGER_H

#ifndef AETHER_CORE_PDU_TRACE_H
#define AETHER_CORE_PDU_TRACE_H

#include "common/logger.h"
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace core {

// Emit a PDU_TRACE log line for the Web LMT PDU inspector.
// Hex payload is capped at 48 bytes; longer PDUs are marked truncated.
inline void trace_pdu(const char* layer, const char* direction,
                      const char* brief, const std::vector<uint8_t>& bytes) {
    constexpr size_t kMaxHexBytes = 48;
    size_t n = std::min(bytes.size(), kMaxHexBytes);
    std::string hex;
    hex.reserve(n * 3 + 8);
    for (size_t i = 0; i < n; ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", bytes[i]);
        hex += buf;
        if (i + 1 < n) hex += ":";
    }
    if (bytes.size() > kMaxHexBytes) hex += ":...";
    LOG_INFO("PDU_TRACE", {{"layer", layer},
                            {"direction", direction},
                            {"len", std::to_string(bytes.size())},
                            {"hex", hex},
                            {"brief", brief}});
}

}

#endif

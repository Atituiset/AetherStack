#include "mac/mac_pdu.h"
#include <cstring>

namespace mac {

std::vector<uint8_t> build_pdu(const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& sdus) {
    std::vector<uint8_t> pdu;
    for (const auto& [lcid, sdu] : sdus) {
        uint16_t len = static_cast<uint16_t>(sdu.size());
        if (len <= 255) {
            uint8_t header_byte = (lcid & 0x3F);
            pdu.push_back(header_byte);
            pdu.push_back(static_cast<uint8_t>(len));
        } else {
            uint8_t header_byte = 0x40 | (lcid & 0x3F); // F=1
            pdu.push_back(header_byte);
            pdu.push_back(static_cast<uint8_t>(len >> 8));
            pdu.push_back(static_cast<uint8_t>(len & 0xFF));
        }
        pdu.insert(pdu.end(), sdu.begin(), sdu.end());
    }
    return pdu;
}

std::vector<std::pair<uint8_t, std::vector<uint8_t>>> parse_pdu(const std::vector<uint8_t>& pdu) {
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> result;
    size_t pos = 0;
    while (pos < pdu.size()) {
        if (pos + 1 >= pdu.size()) break;
        uint8_t header_byte = pdu[pos++];
        uint8_t lcid = header_byte & 0x3F;
        if (lcid == LCID_PADDING) break;
        bool f_bit = (header_byte & 0x40) != 0;
        uint16_t length = 0;
        if (f_bit) {
            if (pos + 2 > pdu.size()) break;
            length = (static_cast<uint16_t>(pdu[pos]) << 8) | pdu[pos + 1];
            pos += 2;
        } else {
            length = pdu[pos++];
        }
        if (pos + length > pdu.size()) break;
        std::vector<uint8_t> sdu(pdu.begin() + pos, pdu.begin() + pos + length);
        result.emplace_back(lcid, std::move(sdu));
        pos += length;
    }
    return result;
}

}

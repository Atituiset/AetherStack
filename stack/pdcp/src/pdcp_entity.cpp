#include "pdcp/pdcp_entity.h"
#include <stdexcept>

namespace pdcp {

std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu;
    pdu.reserve(PDCP_HEADER_SIZE + sdu.size());
    pdu.push_back(0x00);
    pdu.push_back(0x00);
    pdu.insert(pdu.end(), sdu.begin(), sdu.end());
    return pdu;
}

std::vector<uint8_t> rx(const std::vector<uint8_t>& pdu) {
    if (pdu.size() < PDCP_HEADER_SIZE) {
        return {};
    }
    return std::vector<uint8_t>(pdu.begin() + PDCP_HEADER_SIZE, pdu.end());
}

}

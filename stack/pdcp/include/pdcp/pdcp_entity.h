#ifndef AETHER_PDCP_PDCP_ENTITY_H
#define AETHER_PDCP_PDCP_ENTITY_H

#include <cstdint>
#include <vector>

namespace pdcp {

constexpr uint8_t PDCP_HEADER_SIZE = 2;

std::vector<uint8_t> tx(const std::vector<uint8_t>& sdu);
std::vector<uint8_t> rx(const std::vector<uint8_t>& pdu);

}

#endif

#ifndef AETHER_RLC_RLC_TM_H
#define AETHER_RLC_RLC_TM_H

#include <cstdint>
#include <vector>

namespace rlc {

std::vector<uint8_t> tm_tx(const std::vector<uint8_t>& sdu);
std::vector<uint8_t> tm_rx(const std::vector<uint8_t>& pdu);

}

#endif

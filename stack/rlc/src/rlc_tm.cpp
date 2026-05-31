#include "rlc/rlc_tm.h"

namespace rlc {

std::vector<uint8_t> tm_tx(const std::vector<uint8_t>& sdu) {
    return sdu;
}

std::vector<uint8_t> tm_rx(const std::vector<uint8_t>& pdu) {
    return pdu;
}

}

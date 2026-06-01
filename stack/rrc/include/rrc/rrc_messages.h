#ifndef AETHER_RRC_RRC_MESSAGES_H
#define AETHER_RRC_RRC_MESSAGES_H

#include "rrc/rrc_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace rrc {

struct Mib {
    uint16_t sfn = 0;
    uint8_t dl_bandwidth = 50;
    uint8_t phich_config = 0;

    std::vector<uint8_t> encode() const;
    static Mib decode(const std::vector<uint8_t>& data);
};

struct Sib1 {
    std::string plmn_id = "46001";
    uint16_t tac = 1;
    uint16_t cell_id = 1;

    std::vector<uint8_t> encode() const;
    static Sib1 decode(const std::vector<uint8_t>& data);
};

struct RrcMessage {
    RrcMessageType msg_type = RrcMessageType::SETUP_REQUEST;
    std::vector<uint8_t> value;

    std::vector<uint8_t> encode() const;
    static RrcMessage decode(const std::vector<uint8_t>& data);
};

Mib generate_mib(uint16_t sfn);
Sib1 generate_sib1();

}

#endif

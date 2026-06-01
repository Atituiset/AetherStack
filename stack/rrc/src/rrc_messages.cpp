#include "rrc/rrc_messages.h"
#include <cstring>

namespace rrc {

std::vector<uint8_t> Mib::encode() const {
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(RrcMessageType::MIB_BROADCAST));
    data.push_back(static_cast<uint8_t>(sfn & 0xFF));
    data.push_back(static_cast<uint8_t>((sfn >> 8) & 0xFF));
    data.push_back(dl_bandwidth);
    data.push_back(phich_config);
    return data;
}

Mib Mib::decode(const std::vector<uint8_t>& data) {
    Mib mib;
    if (data.size() < 5) return mib;
    mib.sfn = data[1] | (data[2] << 8);
    mib.dl_bandwidth = data[3];
    mib.phich_config = data[4];
    return mib;
}

std::vector<uint8_t> Sib1::encode() const {
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(RrcMessageType::SIB1_BROADCAST));
    for (char c : plmn_id) data.push_back(static_cast<uint8_t>(c));
    data.push_back(0x00);
    data.push_back(static_cast<uint8_t>(tac & 0xFF));
    data.push_back(static_cast<uint8_t>((tac >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(cell_id & 0xFF));
    data.push_back(static_cast<uint8_t>((cell_id >> 8) & 0xFF));
    return data;
}

Sib1 Sib1::decode(const std::vector<uint8_t>& data) {
    Sib1 sib1;
    sib1.plmn_id.clear();
    if (data.size() < 7) return sib1;
    size_t pos = 1;
    while (pos < data.size() && data[pos] != 0x00) {
        sib1.plmn_id += static_cast<char>(data[pos++]);
    }
    if (pos < data.size()) pos++;
    if (pos + 3 < data.size()) {
        sib1.tac = data[pos] | (data[pos + 1] << 8);
        sib1.cell_id = data[pos + 2] | (data[pos + 3] << 8);
    }
    return sib1;
}

std::vector<uint8_t> RrcMessage::encode() const {
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(msg_type));
    uint16_t len = static_cast<uint16_t>(value.size());
    data.push_back(static_cast<uint8_t>(len & 0xFF));
    data.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    data.insert(data.end(), value.begin(), value.end());
    return data;
}

RrcMessage RrcMessage::decode(const std::vector<uint8_t>& data) {
    RrcMessage msg;
    if (data.size() < 3) return msg;
    msg.msg_type = static_cast<RrcMessageType>(data[0]);
    uint16_t len = data[1] | (data[2] << 8);
    if (data.size() >= 3u + len) {
        msg.value.assign(data.begin() + 3, data.begin() + 3 + len);
    }
    return msg;
}

Mib generate_mib(uint16_t sfn) {
    Mib mib;
    mib.sfn = sfn;
    mib.dl_bandwidth = 50;
    mib.phich_config = 0;
    return mib;
}

Sib1 generate_sib1() {
    Sib1 sib1;
    sib1.plmn_id = "46001";
    sib1.tac = 1;
    sib1.cell_id = 1;
    return sib1;
}

}

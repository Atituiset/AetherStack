#include "cn/upf.h"
#include "common/logger.h"

namespace cn {

void Upf::handle(const CnMessage& msg) {
    switch (msg.msg_type) {
        case MsgType::UL_DATA: {
            // {tmsi:4}{rnti:2} ++ payload
            if (msg.value.size() < 6) break;
            const uint32_t tmsi = get32(msg.value, 0);
            const uint16_t rnti = get16(msg.value, 4);
            auto& r = routes_[tmsi];
            if (r.rnti != rnti) {
                // First sighting or a re-route (handover / gNB restart).
                LOG_INFO(ev::UPF_PATH_SWITCH,
                         {{"tmsi", std::to_string(tmsi)},
                          {"rnti", std::to_string(rnti)}});
            }
            r.rnti = rnti;
            std::vector<uint8_t> pdu(msg.value.begin() + 6, msg.value.end());
            if (ul_sink_) ul_sink_(tmsi, pdu); // demo/test tap (echo etc.)
            break;
        }

        case MsgType::PATH_SWITCH: {
            // Handover landed here: retarget the anchor route.
            if (msg.value.size() < 8) break;
            const uint32_t tmsi = get32(msg.value, 0);
            const uint16_t new_rnti = get16(msg.value, 4);
            const uint16_t gnb_cell = get16(msg.value, 6);
            auto& r = routes_[tmsi];
            r.rnti = new_rnti;
            r.gnb_cell = gnb_cell;
            LOG_INFO(ev::UPF_PATH_SWITCH,
                     {{"tmsi", std::to_string(tmsi)},
                      {"cell", std::to_string(gnb_cell)}});
            break;
        }

        default:
            break;
    }
}

void Upf::send_downlink(uint32_t tmsi, const std::vector<uint8_t>& pdu) {
    if (dl_sink_) dl_sink_(tmsi, pdu);
    auto it = routes_.find(tmsi);
    if (it == routes_.end()) {
        LOG_WARN(ev::UPF_NO_ROUTE, {{"tmsi", std::to_string(tmsi)}});
        return;
    }
    std::vector<uint8_t> v;
    put32(v, tmsi);
    put16(v, it->second.rnti);
    v.insert(v.end(), pdu.begin(), pdu.end());
    send(MsgType::DL_DATA, std::move(v));
}

} // namespace cn

#include "mac/rach_bs.h"
#include "common/logger.h"

namespace mac {

void RachBs::set_send_callback(RachSendCallback cb) { send_cb_ = std::move(cb); }
void RachBs::set_state_callback(RachStateCallback cb) { state_cb_ = std::move(cb); }
void RachBs::set_ccch_handler(CcchHandler handler) { ccch_handler_ = std::move(handler); }

void RachBs::on_prach_received(PreambleIndex preamble_idx) {
    RaRnti ra_rnti = 0x4300 | preamble_idx;
    uint16_t ta = 12;
    uint8_t ul_grant = 5;

    UeContext ctx;
    ctx.ra_rnti = ra_rnti;
    ctx.preamble = preamble_idx;
    ctx.c_rnti = 0;
    ctx.rach_complete = false;
    ue_contexts_[ra_rnti] = ctx;

    // Send MSG2: RAR
    std::vector<uint8_t> msg2 = {static_cast<uint8_t>(RachMsgType::MSG2_RAR),
                                  static_cast<uint8_t>(ra_rnti & 0xFF),
                                  static_cast<uint8_t>((ra_rnti >> 8) & 0xFF),
                                  static_cast<uint8_t>(ta & 0xFF),
                                  ul_grant};
    if (send_cb_) send_cb_(RachMsgType::MSG2_RAR, msg2);
    LOG_INFO("MAC_RACH_MSG2", {{"ra_rnti", std::to_string(ra_rnti)},
                                {"preamble", std::to_string(preamble_idx)},
                                {"ta", std::to_string(ta)}});
}

void RachBs::on_msg3_received(RaRnti ra_rnti, const std::vector<uint8_t>& msg3_data) {
    auto it = ue_contexts_.find(ra_rnti);
    if (it == ue_contexts_.end()) {
        LOG_WARN("MSG3_UNKNOWN_RA_RNTI", {{"ra_rnti", std::to_string(ra_rnti)}});
        return;
    }

    uint16_t crnti = next_crnti_++;
    it->second.c_rnti = crnti;

    // Send MSG4: Contention Resolution
    std::vector<uint8_t> msg4 = {static_cast<uint8_t>(RachMsgType::MSG4_CONTENTION_RESOLVE),
                                  static_cast<uint8_t>(crnti & 0xFF),
                                  static_cast<uint8_t>((crnti >> 8) & 0xFF)};
    if (send_cb_) send_cb_(RachMsgType::MSG4_CONTENTION_RESOLVE, msg4);
    LOG_INFO("MAC_RACH_MSG4", {{"ra_rnti", std::to_string(ra_rnti)},
                                {"c_rnti", std::to_string(crnti)}});
    it->second.rach_complete = true;
    LOG_INFO("RA_SUCCESS", {{"c_rnti", std::to_string(crnti)}});

    // MSG3 may carry the CCCH PDU (e.g. RRC SetupRequest) after its header.
    if (msg3_data.size() > 3 && ccch_handler_) {
        ccch_handler_(crnti, {msg3_data.begin() + 3, msg3_data.end()});
    }
}

const RachBs::UeContext* RachBs::find_ue(RaRnti ra_rnti) const {
    auto it = ue_contexts_.find(ra_rnti);
    return it != ue_contexts_.end() ? &it->second : nullptr;
}

bool RachBs::is_rach_complete(RaRnti ra_rnti) const {
    auto it = ue_contexts_.find(ra_rnti);
    return it != ue_contexts_.end() && it->second.rach_complete;
}

}

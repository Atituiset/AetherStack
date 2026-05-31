#include "mac/rach_ue.h"
#include "common/logger.h"

namespace mac {

RachUe::RachUe(const RachConfig& config) : config_(config) {}

void RachUe::set_send_callback(RachSendCallback cb) { send_cb_ = std::move(cb); }
void RachUe::set_state_callback(RachStateCallback cb) { state_cb_ = std::move(cb); }

void RachUe::transition(RachState new_state) {
    RachState old = state_;
    state_ = new_state;
    LOG_INFO("MAC_STATE_CHANGE", {{"layer", "RACH"},
                                   {"old_state", rach_state_str(old)},
                                   {"new_state", rach_state_str(new_state)}});
    if (state_cb_) state_cb_(old, new_state);
}

void RachUe::start_rach() {
    if (state_ != RachState::IDLE) {
        LOG_WARN("RACH_START_IGNORED", {{"state", rach_state_str(state_)}});
        return;
    }
    preamble_tx_count_ = 1;
    assigned_ra_rnti_ = 0;
    assigned_crnti_ = 0;

    // Send MSG1: PRACH preamble
    std::vector<uint8_t> msg1 = {static_cast<uint8_t>(RachMsgType::MSG1_PRACH),
                                  config_.preamble_index};
    if (send_cb_) send_cb_(RachMsgType::MSG1_PRACH, msg1);
    LOG_INFO("MAC_RACH_MSG1", {{"preamble", std::to_string(config_.preamble_index)},
                                {"tx_count", std::to_string(preamble_tx_count_)}});

    transition(RachState::WAIT_RAR);
}

void RachUe::on_rar_received(RaRnti ra_rnti, uint16_t timing_advance, uint8_t ul_grant) {
    if (state_ != RachState::WAIT_RAR) {
        LOG_WARN("RAR_IGNORED", {{"state", rach_state_str(state_)}});
        return;
    }
    assigned_ra_rnti_ = ra_rnti;
    LOG_INFO("MAC_RACH_MSG2_RX", {{"ra_rnti", std::to_string(ra_rnti)},
                                   {"ta", std::to_string(timing_advance)}});

    // Send MSG3: simplified RRC Setup Request (just placeholder)
    std::vector<uint8_t> msg3 = {static_cast<uint8_t>(RachMsgType::MSG3_RRC_REQ),
                                  static_cast<uint8_t>(ra_rnti & 0xFF),
                                  static_cast<uint8_t>((ra_rnti >> 8) & 0xFF)};
    if (send_cb_) send_cb_(RachMsgType::MSG3_RRC_REQ, msg3);
    LOG_INFO("MAC_RACH_MSG3", {{"ra_rnti", std::to_string(ra_rnti)}});

    transition(RachState::WAIT_CONTENTION_RESOLVE);
}

void RachUe::on_contention_resolve(uint16_t crnti) {
    if (state_ != RachState::WAIT_CONTENTION_RESOLVE) {
        LOG_WARN("CR_IGNORED", {{"state", rach_state_str(state_)}});
        return;
    }
    assigned_crnti_ = crnti;
    LOG_INFO("MAC_RACH_MSG4_RX", {{"c_rnti", std::to_string(crnti)}});
    LOG_INFO("RACH_SUCCESS", {{"c_rnti", std::to_string(crnti)}});
    transition(RachState::CONNECTED);
}

void RachUe::on_rar_timeout() {
    if (state_ != RachState::WAIT_RAR) return;
    preamble_tx_count_++;
    if (preamble_tx_count_ <= config_.max_preamble_transmissions) {
        LOG_WARN("RACH_RAR_TIMEOUT", {{"retry", std::to_string(preamble_tx_count_)}});

        std::vector<uint8_t> msg1 = {static_cast<uint8_t>(RachMsgType::MSG1_PRACH),
                                      config_.preamble_index};
        if (send_cb_) send_cb_(RachMsgType::MSG1_PRACH, msg1);
        LOG_INFO("MAC_RACH_MSG1", {{"preamble", std::to_string(config_.preamble_index)},
                                    {"tx_count", std::to_string(preamble_tx_count_)}});
    } else {
        LOG_ERROR("RACH_FAILED", {{"reason", "max_retries"}});
        preamble_tx_count_ = 0;
        transition(RachState::IDLE);
    }
}

void RachUe::on_contention_resolve_timeout() {
    if (state_ != RachState::WAIT_CONTENTION_RESOLVE) return;
    LOG_ERROR("RACH_CR_TIMEOUT", {});
    transition(RachState::IDLE);
}

}

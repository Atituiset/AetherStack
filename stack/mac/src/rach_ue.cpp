#include "mac/rach_ue.h"
#include "common/logger.h"

namespace mac {

RachUe::RachUe(const RachConfig& config) : config_(config) {}

void RachUe::set_send_callback(RachSendCallback cb) { send_cb_ = std::move(cb); }
void RachUe::set_state_callback(RachStateCallback cb) { state_cb_ = std::move(cb); }
void RachUe::set_msg3_provider(Msg3Provider provider) { msg3_provider_ = std::move(provider); }

void RachUe::transition(RachState new_state) {
    RachState old = state_;
    state_ = new_state;
    LOG_INFO(ev::MAC_STATE_CHANGE, {{"layer", "RACH"},
                                   {"old_state", rach_state_str(old)},
                                   {"new_state", rach_state_str(new_state)}});
    if (state_cb_) state_cb_(old, new_state);
}

void RachUe::start_rach() {
    if (state_ != RachState::IDLE) {
        LOG_WARN(ev::RACH_START_IGNORED, {{"state", rach_state_str(state_)}});
        return;
    }
    preamble_tx_count_ = 1;
    assigned_ra_rnti_ = 0;
    assigned_crnti_ = 0;

    // Transition first: with a synchronous in-memory loop the MSG2 reply
    // arrives inside the MSG1 send callback, so WAIT_RAR must already hold.
    transition(RachState::WAIT_RAR);

    // Send MSG1: PRACH preamble
    std::vector<uint8_t> msg1 = {static_cast<uint8_t>(RachMsgType::MSG1_PRACH),
                                  config_.preamble_index};
    if (send_cb_) send_cb_(RachMsgType::MSG1_PRACH, msg1);
    LOG_INFO(ev::MAC_RACH_MSG1, {{"preamble", std::to_string(config_.preamble_index)},
                                {"tx_count", std::to_string(preamble_tx_count_)}});
}

void RachUe::on_rar_received(RaRnti ra_rnti, uint16_t timing_advance, uint8_t ul_grant) {
    if (state_ != RachState::WAIT_RAR) {
        LOG_WARN(ev::RAR_IGNORED, {{"state", rach_state_str(state_)}});
        return;
    }
    // Shared medium: accept only the RAR answering our own preamble.
    if (ra_rnti != ra_rnti_for_preamble(config_.preamble_index)) {
        LOG_DEBUG(ev::RAR_IGNORED, {{"reason", "ra_rnti_mismatch"},
                                    {"ra_rnti", std::to_string(ra_rnti)}});
        return;
    }
    assigned_ra_rnti_ = ra_rnti;
    LOG_INFO(ev::MAC_RACH_MSG2_RX, {{"ra_rnti", std::to_string(ra_rnti)},
                                   {"ta", std::to_string(timing_advance)}});

    // Same ordering rule as start_rach: be in WAIT_CONTENTION_RESOLVE
    // before MSG3 (and thus MSG4) hit the wire.
    transition(RachState::WAIT_CONTENTION_RESOLVE);

    // Send MSG3: [type][ra_rnti LE][optional CCCH payload from provider]
    std::vector<uint8_t> msg3 = {static_cast<uint8_t>(RachMsgType::MSG3_RRC_REQ),
                                  static_cast<uint8_t>(ra_rnti & 0xFF),
                                  static_cast<uint8_t>((ra_rnti >> 8) & 0xFF)};
    if (msg3_provider_) {
        auto ccch = msg3_provider_();
        msg3.insert(msg3.end(), ccch.begin(), ccch.end());
    }
    if (send_cb_) send_cb_(RachMsgType::MSG3_RRC_REQ, msg3);
    LOG_INFO(ev::MAC_RACH_MSG3, {{"ra_rnti", std::to_string(ra_rnti)},
                                {"ccch_len", std::to_string(msg3.size() - 3)}});
}

void RachUe::on_contention_resolve(uint16_t crnti, RaRnti ra_rnti) {
    if (state_ != RachState::WAIT_CONTENTION_RESOLVE) {
        LOG_WARN(ev::CR_IGNORED, {{"state", rach_state_str(state_)}});
        return;
    }
    // Shared medium: claim the C-RNTI only from our own context's MSG4.
    if (ra_rnti != assigned_ra_rnti_) {
        LOG_DEBUG(ev::CR_IGNORED, {{"reason", "ra_rnti_mismatch"},
                                   {"ra_rnti", std::to_string(ra_rnti)}});
        return;
    }
    assigned_crnti_ = crnti;
    LOG_INFO(ev::MAC_RACH_MSG4_RX, {{"c_rnti", std::to_string(crnti)}});
    LOG_INFO(ev::RACH_SUCCESS, {{"c_rnti", std::to_string(crnti)}});
    transition(RachState::CONNECTED);
}

void RachUe::on_rar_timeout() {
    if (state_ != RachState::WAIT_RAR) return;
    preamble_tx_count_++;
    if (preamble_tx_count_ <= config_.max_preamble_transmissions) {
        LOG_WARN(ev::RACH_RAR_TIMEOUT, {{"retry", std::to_string(preamble_tx_count_)}});

        std::vector<uint8_t> msg1 = {static_cast<uint8_t>(RachMsgType::MSG1_PRACH),
                                      config_.preamble_index};
        if (send_cb_) send_cb_(RachMsgType::MSG1_PRACH, msg1);
        LOG_INFO(ev::MAC_RACH_MSG1, {{"preamble", std::to_string(config_.preamble_index)},
                                    {"tx_count", std::to_string(preamble_tx_count_)}});
    } else {
        LOG_ERROR(ev::RACH_FAILED, {{"reason", "max_retries"}});
        preamble_tx_count_ = 0;
        transition(RachState::IDLE);
    }
}

void RachUe::on_contention_resolve_timeout() {
    if (state_ != RachState::WAIT_CONTENTION_RESOLVE) return;
    LOG_ERROR(ev::RACH_CR_TIMEOUT, {});
    transition(RachState::IDLE);
}

void RachUe::force_idle() {
    if (state_ != RachState::IDLE) {
        transition(RachState::IDLE);
    }
}

}

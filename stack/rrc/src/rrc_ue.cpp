#include "rrc/rrc_ue.h"
#include "common/logger.h"

namespace rrc {

void RrcUe::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

void RrcUe::transition(UeState new_state) {
    UeState old = state_;
    state_ = new_state;
    LOG_INFO(ev::RRC_UE_STATE, {{"old", ue_state_str(old)}, {"new", ue_state_str(new_state)}});
}

void RrcUe::on_mib_received(const Mib& mib) {
    received_mib_ = mib;
    LOG_INFO(ev::RRC_MIB_RX, {{"sfn", std::to_string(mib.sfn)},
                             {"bw", std::to_string(mib.dl_bandwidth)}});
}

void RrcUe::on_sib1_received(const Sib1& sib1) {
    received_sib1_ = sib1;
    LOG_INFO(ev::RRC_SIB1_RX, {{"plmn", sib1.plmn_id},
                              {"tac", std::to_string(sib1.tac)},
                              {"cell_id", std::to_string(sib1.cell_id)}});
}

void RrcUe::restore_connected(uint16_t crnti) {
    assigned_crnti_ = crnti;
    if (state_ != UeState::CONNECTED) transition(UeState::CONNECTED);
}

void RrcUe::start_connection() {    if (state_ != UeState::IDLE) {
        LOG_WARN(ev::RRC_SETUP_IGNORED, {{"state", ue_state_str(state_)}});
        return;
    }

    RrcMessage msg;
    msg.msg_type = RrcMessageType::SETUP_REQUEST;
    msg.value = {0x00};
    auto encoded = msg.encode();

    transition(UeState::CONNECTING); // see nas_ue.cpp: state before transmit

    if (send_cb_) send_cb_(encoded);
    LOG_INFO(ev::RRC_SETUP_REQUEST_TX, {});
}

void RrcUe::release() {
    if (state_ != UeState::CONNECTED) {
        LOG_WARN(ev::RRC_RELEASE_IGNORED, {{"state", ue_state_str(state_)}});
        return;
    }

    uint16_t crnti = assigned_crnti_;
    RrcMessage msg;
    msg.msg_type = RrcMessageType::RELEASE;
    msg.value = {static_cast<uint8_t>(crnti & 0xFF),
                 static_cast<uint8_t>((crnti >> 8) & 0xFF)};
    auto encoded = msg.encode();

    transition(UeState::IDLE); // state before transmit
    assigned_crnti_ = 0;

    if (send_cb_) send_cb_(encoded);
    LOG_INFO(ev::RRC_RELEASE_TX, {{"c_rnti", std::to_string(crnti)}});
}

void RrcUe::force_idle() {
    if (state_ == UeState::IDLE) return;
    assigned_crnti_ = 0;
    transition(UeState::IDLE);
}

void RrcUe::on_message(const std::vector<uint8_t>& pdu) {
    auto msg = RrcMessage::decode(pdu);
    if (msg.msg_type == RrcMessageType::SETUP) {
        if (state_ != UeState::CONNECTING) {
            LOG_WARN(ev::RRC_SETUP_RX_IGNORED, {{"state", ue_state_str(state_)}});
            return;
        }
        if (msg.value.size() >= 2) {
            assigned_crnti_ = msg.value[0] | (msg.value[1] << 8);
        }
        LOG_INFO(ev::RRC_SETUP_RX, {{"c_rnti", std::to_string(assigned_crnti_)}});

        RrcMessage complete;
        complete.msg_type = RrcMessageType::SETUP_COMPLETE;
        complete.value = {static_cast<uint8_t>(assigned_crnti_ & 0xFF),
                          static_cast<uint8_t>((assigned_crnti_ >> 8) & 0xFF)};
        auto encoded = complete.encode();

        transition(UeState::CONNECTED); // state before transmit

        if (send_cb_) send_cb_(encoded);
        LOG_INFO(ev::RRC_SETUP_COMPLETE_TX, {{"c_rnti", std::to_string(assigned_crnti_)}});
    } else if (msg.msg_type == RrcMessageType::RELEASE) {
        assigned_crnti_ = 0;
        transition(UeState::IDLE);
        LOG_INFO(ev::RRC_RELEASED, {});
    }
}

}

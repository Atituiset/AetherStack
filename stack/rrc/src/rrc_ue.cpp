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

void RrcUe::start_connection(uint16_t cell_id) {
    if (state_ != UeState::IDLE) {
        LOG_WARN(ev::RRC_SETUP_IGNORED, {{"state", ue_state_str(state_)}});
        return;
    }

    RrcMessage msg;
    msg.msg_type = RrcMessageType::SETUP_REQUEST;
    if (cell_id != 0) {
        // M22: name the target cell so the other cell on the shared
        // medium stays silent.
        msg.value = {static_cast<uint8_t>(cell_id & 0xFF),
                     static_cast<uint8_t>((cell_id >> 8) & 0xFF)};
    } else {
        msg.value = {0x00};
    }
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

void RrcUe::request_suspend() {
    if (state_ != UeState::CONNECTED) {
        LOG_WARN(ev::RRC_RELEASE_IGNORED, {{"state", ue_state_str(state_)}});
        return;
    }
    const uint16_t crnti = assigned_crnti_;
    RrcMessage msg;
    msg.msg_type = RrcMessageType::RELEASE;
    msg.value = {static_cast<uint8_t>(crnti & 0xFF),
                 static_cast<uint8_t>((crnti >> 8) & 0xFF),
                 0x02}; // flags bit1: suspend request (M20)
    if (send_cb_) send_cb_(msg.encode());
    LOG_INFO(ev::RRC_RELEASE_TX,
             {{"c_rnti", std::to_string(crnti)}, {"suspend", "request"}});
}

void RrcUe::force_idle() {
    if (state_ == UeState::IDLE) return;
    assigned_crnti_ = 0;
    resume_id_ = 0;
    suspended_crnti_ = 0;
    transition(UeState::IDLE);
}

// M20: begin a resume (the node drives RACH with the request PDU).
void RrcUe::start_resume() {
    if (state_ != UeState::INACTIVE) return;
    transition(UeState::CONNECTING);
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
        // M20: [crnti:2][flags:1][resume_id:4] — flags bit0 suspends us
        // with a resume identity instead of a plain release.
        if (msg.value.size() >= 7 && (msg.value[2] & 0x01) != 0 &&
            state_ == UeState::CONNECTED) {
            suspended_crnti_ = assigned_crnti_;
            resume_id_ = 0;
            for (int i = 0; i < 4; ++i) {
                resume_id_ |= static_cast<uint32_t>(msg.value[3 + i])
                              << (8 * i);
            }
            transition(UeState::INACTIVE);
            LOG_INFO(ev::RRC_INACTIVE,
                     {{"c_rnti", std::to_string(suspended_crnti_)},
                      {"resume_id", std::to_string(resume_id_)}});
            if (suspend_cb_) suspend_cb_(resume_id_);
            return;
        }
        assigned_crnti_ = 0;
        transition(UeState::IDLE);
        LOG_INFO(ev::RRC_RELEASED, {});
    } else if (msg.msg_type == RrcMessageType::RESUME_OK) {
        // M20: context restored, possibly under a fresh C-RNTI.
        if (state_ != UeState::CONNECTING || resume_id_ == 0) return;
        uint16_t new_crnti = assigned_crnti_;
        if (msg.value.size() >= 2) {
            new_crnti = static_cast<uint16_t>(msg.value[0] |
                                              (msg.value[1] << 8));
        }
        const uint16_t old = suspended_crnti_;
        assigned_crnti_ = new_crnti;
        suspended_crnti_ = 0;
        resume_id_ = 0;
        transition(UeState::CONNECTED);
        LOG_INFO(ev::RRC_RESUMED,
                 {{"c_rnti", std::to_string(new_crnti)},
                  {"old_c_rnti", std::to_string(old)}});
        if (resume_cb_) resume_cb_(true, new_crnti);
    } else if (msg.msg_type == RrcMessageType::RESUME_FAILURE) {
        // M20: the network no longer knows our resume identity — the node
        // falls back to a full setup.
        if (state_ != UeState::CONNECTING) return;
        LOG_WARN(ev::RRC_RESUME_FAIL, {{"reason", "stale_id"}});
        assigned_crnti_ = 0;
        suspended_crnti_ = 0;
        resume_id_ = 0;
        transition(UeState::IDLE);
        if (resume_cb_) resume_cb_(false, 0);
    }
}

}

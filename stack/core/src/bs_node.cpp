#include "core/bs_node.h"
#include "core/pdu_trace.h"
#include "mac/mac_pdu.h"
#include "nas/nas_messages.h"
#include "pdcp/pdcp_entity.h"
#include "rlc/rlc_tm.h"
#include "rrc/rrc_messages.h"
#include <algorithm>

namespace core {

BsNode::BsNode(const BsNodeConfig& config) : config_(config) {
    rrc_bs_.set_cell_identity(config_.cell_id, config_.plmn_id, config_.tac);

    // M14: a successful re-establishment migrated the RRC context to a new
    // C-RNTI — move the data-path state (flow, security, AM entities) with it.
    rrc_bs_.set_reest_callback([this](uint16_t old_rnti, uint16_t new_rnti) {
        auto it = flows_.find(old_rnti);
        if (it != flows_.end()) {
            auto f = std::move(it->second);
            flows_.erase(it);
            flows_[new_rnti] = std::move(f);
        }
        for (auto& [tmsi, crnti] : tmsi_to_crnti_) {
            if (crnti == old_rnti) { crnti = new_rnti; break; }
        }
    });

    rach_bs_.set_send_callback([this](mac::RachMsgType type, const std::vector<uint8_t>& pdu) {
        AirFrameType frame_type = AirFrameType::DATA;
        switch (type) {
            case mac::RachMsgType::MSG2_RAR: frame_type = AirFrameType::MSG2_RAR; break;
            case mac::RachMsgType::MSG4_CONTENTION_RESOLVE:
                frame_type = AirFrameType::MSG4_CR;
                break;
            default:
                LOG_WARN(ev::BS_RACH_TX_UNEXPECTED, {{"type", std::to_string(static_cast<int>(type))}});
                return;
        }
        send_frame(frame_type, 0, pdu);
    });

    // MSG3 carries the CCCH PDU (RRC SetupRequest) after its 3-byte header.
    rach_bs_.set_ccch_handler([this](uint16_t crnti, const std::vector<uint8_t>& ccch) {
        rrc_bs_.handle_message(crnti, ccch);
    });

    rrc_bs_.set_send_callback([this](uint16_t rnti, const std::vector<uint8_t>& pdu) {
        downlink_send(rnti, mac::LCID_CCCH, pdu);
    });

    nas_bs_.set_send_callback([this](uint32_t tmsi, const std::vector<uint8_t>& pdu) {
        uint16_t crnti = ul_crnti_;
        auto it = tmsi_to_crnti_.find(tmsi);
        if (it != tmsi_to_crnti_.end()) {
            crnti = it->second;
        } else {
            tmsi_to_crnti_[tmsi] = ul_crnti_; // fresh accept for this UE
        }
        // M12: an authenticated attach carries the session key out of the
        // HSS emulation. The ATTACH_ACCEPT itself still goes in the clear;
        // encryption switches on right after it has been queued.
        const auto* new_key = nas_bs_.session_key(tmsi);
        bool arm_sec = new_key != nullptr;
        std::array<uint8_t, crypto::kKey256Size> key_copy{};
        if (new_key) key_copy = *new_key;

        auto wrapped = pdcp::tx(rlc::tm_tx(pdu));
        downlink_send(crnti, mac::LCID_NAS_DCCH, wrapped);

        if (arm_sec) {
            auto& f = flow(crnti);
            f.up_key = key_copy;
            f.sec_on = true;
            LOG_INFO(ev::SEC_ENABLED,
                     {{"dir", "dl"}, {"rnti", std::to_string(crnti)}});
        }
    });
}

void BsNode::set_air_send(AirBitsSend send) { air_send_ = std::move(send); }

// ---- M15: core-network separation -------------------------------------------

void BsNode::attach_core(CnEndpoints ep) {
    cn_amf_ = ep.amf;
    cn_upf_ = ep.upf;
    if (cn_amf_) {
        cn_amf_->set_handler(
            [this](const cn::CnMessage& m) { handle_cn_message(m); });
        cn::CnMessage setup;
        setup.msg_type = cn::MsgType::NG_SETUP;
        cn::put16(setup.value, ep.gnb_cell);
        cn_amf_->send(setup);
    }
    if (cn_upf_) {
        // Both carriers deliver core->gNB messages through the same handler.
        cn_upf_->set_handler(
            [this](const cn::CnMessage& m) { handle_cn_message(m); });
    }
}

void BsNode::handle_cn_message(const cn::CnMessage& msg) {
    switch (msg.msg_type) {
        case cn::MsgType::NG_SETUP_OK:
            ng_setup_ok_ = true;
            LOG_INFO(ev::NG_SETUP_RX, {{"result", "ok"}});
            break;

        case cn::MsgType::DOWNLINK_NAS: {
            // {tmsi:4}{rnti:2}{len:2} ++ nas_pdu
            if (msg.value.size() < 10) break;
            const uint32_t tmsi = cn::get32(msg.value, 0);
            uint16_t rnti = cn::get16(msg.value, 4);
            auto it = tmsi_to_crnti_.find(tmsi);
            if (it != tmsi_to_crnti_.end()) rnti = it->second;
            else tmsi_to_crnti_[tmsi] = rnti; // AMF told us who this UE is
            deliver_dl_nas(tmsi, rnti,
                           std::vector<uint8_t>(msg.value.begin() + 8,
                                                msg.value.end()));
            break;
        }

        case cn::MsgType::SESSION_KEY: {
            // {tmsi:4}{rnti:2} key(32) — arm user-plane security on the flow.
            if (msg.value.size() < 6 + crypto::kKey256Size) break;
            const uint32_t tmsi = cn::get32(msg.value, 0);
            uint16_t rnti = cn::get16(msg.value, 4);
            auto it = tmsi_to_crnti_.find(tmsi);
            if (it != tmsi_to_crnti_.end()) rnti = it->second;
            else tmsi_to_crnti_[tmsi] = rnti;
            auto& f = flow(rnti);
            std::copy(msg.value.begin() + 6, msg.value.end(), f.up_key.begin());
            f.sec_on = true;
            LOG_INFO(ev::SEC_ENABLED,
                     {{"dir", "dl"}, {"rnti", std::to_string(rnti)}});
            break;
        }

        case cn::MsgType::DL_DATA: {
            // UPF-routed downlink user-plane data for one of our UEs.
            if (msg.value.size() < 6) break;
            const uint16_t rnti = cn::get16(msg.value, 4);
            std::vector<uint8_t> data(msg.value.begin() + 6, msg.value.end());
            auto& f = flow(rnti);
            for (const auto& pdu : f.dl_am_tx.tx(now_ms_, data)) {
                downlink_send(rnti, mac::LCID_APP_DTCH,
                              pdcp::tx(rlc::tm_tx(pdu)));
            }
            break;
        }

        default:
            break;
    }
}

void BsNode::deliver_dl_nas(uint32_t /*tmsi*/, uint16_t rnti,
                            const std::vector<uint8_t>& nas_pdu) {
    auto wrapped = pdcp::tx(rlc::tm_tx(nas_pdu));
    downlink_send(rnti, mac::LCID_NAS_DCCH, wrapped);
}

void BsNode::cn_send_nas(uint32_t tmsi, uint16_t rnti,
                         const std::vector<uint8_t>& nas_pdu) {
    if (!cn_amf_) return;
    cn::CnMessage m;
    if (tmsi == 0) {
        // INITIAL_UE_MSG: first NAS from an unknown UE.
        m.msg_type = cn::MsgType::INITIAL_UE_MSG;
        cn::put16(m.value, rnti);
    } else {
        m.msg_type = cn::MsgType::UPLINK_NAS;
        cn::put32(m.value, tmsi);
        cn::put16(m.value, rnti);
        m.value.push_back(static_cast<uint8_t>(nas_pdu.size() & 0xFF));
        m.value.push_back(static_cast<uint8_t>((nas_pdu.size() >> 8) & 0xFF));
    }
    m.value.insert(m.value.end(), nas_pdu.begin(), nas_pdu.end());
    cn_amf_->send(m);
}

void BsNode::cn_send_uplink_data(uint32_t tmsi, uint16_t rnti,
                                 const std::vector<uint8_t>& data) {
    if (!cn_upf_) return;
    cn::CnMessage m;
    m.msg_type = cn::MsgType::UL_DATA;
    cn::put32(m.value, tmsi);
    cn::put16(m.value, rnti);
    m.value.insert(m.value.end(), data.begin(), data.end());
    cn_upf_->send(m);
}


void BsNode::tick(uint32_t now_ms) {
    now_ms_ = std::max(now_ms, now_ms_);
    timers_.tick(now_ms_);
    schedule_downlink();
}

BsNode::DlFlow& BsNode::flow(uint16_t rnti) {
    auto it = flows_.find(rnti);
    if (it == flows_.end()) {
        it = flows_.emplace(rnti, DlFlow{}).first;
    }
    return it->second;
}

// Fair full pass: every connected flow may hand ONE new transport block to
// its HARQ entity per tick. Uplink stays grant-free (configured grants in
// 3GPP terms); only the shared downlink needs scheduling.
void BsNode::schedule_downlink() {
    pump_flows();
    for (auto& [rnti, f] : flows_) {
        if (f.queue.empty() || f.harq_tx.in_flight() >= 4) continue;
        auto tb = std::move(f.queue.front());
        f.queue.pop_front();
        if (auto e = f.harq_tx.send(tb)) {
            trace_pdu("MAC", "TX", "dl scheduled", e->coded);
            send_frame(AirFrameType::DATA, rnti, e->coded);
        } else {
            f.queue.push_front(std::move(tb));
        }
    }
}

void BsNode::pump_flows() {
    for (auto& [rnti, f] : flows_) {
        for (auto& e : f.harq_tx.poll_timeouts(now_ms_)) {
            trace_pdu("HARQ", "TX", "retx", e.coded);
            send_frame(AirFrameType::DATA, rnti, e.coded);
        }
        // M13: downlink AM liveness probe when the UE's STATUS went missing.
        for (const auto& pdu : f.dl_am_tx.tick(now_ms_)) {
            downlink_send(rnti, mac::LCID_APP_DTCH, pdcp::tx(rlc::tm_tx(pdu)));
        }
        // Retransmitting blocks stay busy until acked or dropped; the
        // scheduler only pulls new blocks when capacity allows.
    }
}

void BsNode::send_ack(uint16_t to, const HarqRx::Result& res) {
    if (!res.need_feedback || to == 0 || to == mac::RNTI_BROADCAST) return;
    if (res.proc == 0x7F) return;
    auto& f = flow(to);
    f.harq_tx.advance(now_ms_);
    downlink_raw(to, mac::LCID_HARQ_ACK,
                 {static_cast<uint8_t>(res.proc & 0x7F),
                  static_cast<uint8_t>(res.ack ? 1 : 0)});
}

void BsNode::start_broadcast() {
    broadcasting_ = true;
    broadcast_sib();
    timers_.schedule(config_.sib_period_ms, true, [this] { broadcast_sib(); });
}

void BsNode::broadcast_sib() {
    if (!broadcasting_ || !sib_enabled_) return;
    auto mib_pdu = rrc_bs_.broadcast_mib().encode();
    auto sib1_pdu = rrc_bs_.broadcast_sib1().encode();
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> entries = {
        {mac::LCID_MIB, mib_pdu}, {mac::LCID_SIB1, sib1_pdu}};
    // M14: one-shot paging record rides along with the system information.
    if (!paging_target_.empty()) {
        entries.emplace_back(
            mac::LCID_PAGING,
            std::vector<uint8_t>(paging_target_.begin(), paging_target_.end()));
        LOG_INFO(ev::PAGE_TX, {{"imsi", paging_target_}});
        paging_target_.clear();
    }
    std::vector<uint8_t> pdu = mac::build_pdu(entries);
    // Broadcasts use the same FEC framing but a dedicated process id and no
    // feedback (nothing to combine against across multiple receivers).
    auto payload = link_encode(pdu, 0x7F, 1);
    trace_pdu("MAC", "TX", "sib broadcast", pdu);
    send_frame(AirFrameType::DATA, mac::RNTI_BROADCAST, payload);
}

void BsNode::on_air_bits(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    if (!unpack_air_bits(bits, bytes)) return;
    AirFrame frame;
    if (!decode_frame(bytes.data(), bytes.size(), frame)) {
        LOG_WARN(ev::AIR_FRAME_DECODE_FAIL, {{"len", std::to_string(bits.size())}});
        return;
    }
    handle_air_frame(frame);
}

void BsNode::handle_air_frame(const AirFrame& frame) {
    switch (frame.type) {
        case AirFrameType::MSG1_PRACH:
            if (frame.payload.size() >= 2) {
                rach_bs_.on_prach_received(frame.payload[1]);
            }
            break;
        case AirFrameType::MSG3_CCCH: {
            if (frame.payload.size() >= 4) {
                uint16_t ra_rnti = static_cast<uint16_t>(frame.payload[1] |
                                                         (frame.payload[2] << 8));
                rach_bs_.on_msg3_received(ra_rnti, frame.payload);
            }
            break;
        }
        case AirFrameType::DATA:
            handle_dl_data(frame.rnti, frame.payload);
            break;
        default: // MSG2/MSG4 are downlink-only
            break;
    }
}

void BsNode::handle_dl_data(uint16_t rnti, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> pdu;
    if (is_harq_framed(payload)) {
        if (!rrc_bs_.find_ue(rnti)) return;
        // UE uplink transport block: decode on this flow's RX entity.
        auto& rx_flow = flow(rnti);
        auto res = rx_flow.harq_rx.receive(payload);
        send_ack(rnti, res); // feedback for the UE's uplink block
        if (!res.delivered) return;
        pdu = std::move(res.mac_pdu);
    } else {
        pdu = payload; // HARQ-ACK control frames arrive unframed
    }
    trace_pdu("MAC", "RX", "ul data", pdu);
    ul_crnti_ = rnti;
    for (auto& [lcid, sdu] : mac::parse_pdu(pdu)) {
        if (sdu.empty()) continue;
        switch (lcid) {
            case mac::LCID_CCCH: {
                trace_pdu("RRC", "RX", "ccch", sdu);
                handle_ccch_sdu(rnti, sdu);
                break;
            }
            case mac::LCID_NAS_DCCH: {
                // The protected frame carries the same legacy-wrapped PDCP
                // payload as the clear path; decrypt first, then unwrap.
                std::vector<uint8_t> nas_pdu;
                auto& rf = flow(rnti);
                if (rf.sec_on) {
                    std::vector<uint8_t> inner;
                    if (!pdcp::unprotect(rf.up_key, sdu, inner)) {
                        LOG_WARN(ev::SEC_DECRYPT_FAIL, {{"layer", "NAS"}});
                        break;
                    }
                    nas_pdu = pdcp::rx(rlc::tm_rx(inner));
                } else {
                    nas_pdu = pdcp::rx(rlc::tm_rx(sdu));
                }
                trace_pdu("NAS", "RX", "dcch", nas_pdu);
                if (core_separated()) {
                    // M15: NAS is opaque to the gNB — tunnel it to the AMF.
                    const uint32_t tmsi = tmsi_for_crnti(rnti);
                    cn_send_nas(tmsi, rnti, nas_pdu);
                    auto decoded = nas::NasMessage::decode(nas_pdu);
                    if (decoded.msg_type == nas::NasMessageType::DETACH) {
                        for (auto it = tmsi_to_crnti_.begin();
                             it != tmsi_to_crnti_.end();) {
                            if (it->second == rnti) it = tmsi_to_crnti_.erase(it);
                            else ++it;
                        }
                        flows_.erase(rnti);
                    }
                    break;
                }
                auto decoded = nas::NasMessage::decode(nas_pdu);
                bool detach = decoded.msg_type == nas::NasMessageType::DETACH;
                nas_bs_.handle_message(0, nas_pdu);
                if (detach) {
                    for (auto it = tmsi_to_crnti_.begin();
                         it != tmsi_to_crnti_.end();) {
                        if (it->second == rnti) it = tmsi_to_crnti_.erase(it);
                        else ++it;
                    }
                    flows_.erase(rnti); // tear down the UE's whole flow
                }
                break;
            }
            case mac::LCID_HARQ_ACK: {
                // UE feedback for our downlink transport blocks.
                if (sdu.size() < 2) break;
                uint8_t proc = sdu[0] & 0x7F;
                auto& dl = flow(rnti).harq_tx;
                dl.advance(now_ms_);
                if (sdu[1]) {
                    dl.on_ack(proc);
                } else if (auto e = dl.on_nack(proc)) {
                    trace_pdu("HARQ", "TX", "retx(nack)", e->coded);
                    send_frame(AirFrameType::DATA, rnti, e->coded);
                }
                break;
            }
            case mac::LCID_APP_DTCH: {
                std::vector<uint8_t> am_pdu;
                auto& rxf = flow(rnti);
                if (rxf.sec_on) {
                    std::vector<uint8_t> inner;
                    if (!pdcp::unprotect(rxf.up_key, sdu, inner)) {
                        LOG_WARN(ev::SEC_DECRYPT_FAIL, {{"layer", "APP"}});
                        break;
                    }
                    am_pdu = pdcp::rx(rlc::tm_rx(inner));
                } else {
                    am_pdu = pdcp::rx(rlc::tm_rx(sdu));
                }
                // M13: RLC AM data PDU — deliver whole SDUs and echo each
                // through the downlink AM entity (downlink_send re-encrypts).
                auto am_out = rxf.ul_am_rx.rx(am_pdu);
                for (const auto& data : am_out.delivered) {
                    trace_pdu("APP", "RX", "ping", data);
                    if (cn_upf_) {
                        // M15: uplink bearer terminates at the UPF anchor.
                        LOG_INFO(ev::APP_ECHO_TX,
                                 {{"len", std::to_string(data.size())},
                                  {"upf", "1"}});
                        cn_send_uplink_data(tmsi_for_crnti(rnti), rnti, data);
                        continue;
                    }
                    LOG_INFO(ev::APP_ECHO_TX,
                             {{"len", std::to_string(data.size())}});
                    for (const auto& pdu :
                         rxf.dl_am_tx.tx(now_ms_, data)) {
                        downlink_send(rnti, mac::LCID_APP_DTCH,
                                      pdcp::tx(rlc::tm_tx(pdu)));
                    }
                }
                if (am_out.status_needed) {
                    auto status = rxf.ul_am_rx.build_status();
                    LOG_INFO(ev::RLC_AM_STATUS_TX,
                             {{"dir", "dl"}, {"nacks", std::to_string(status[2])}});
                    downlink_send(rnti, mac::LCID_RLC_STATUS, status);
                }
                break;
            }
            case mac::LCID_RLC_STATUS: {
                // UE reports holes in our downlink AM stream: retransmit.
                auto& sf = flow(rnti);
                for (const auto& pdu : sf.dl_am_tx.on_status(now_ms_, sdu)) {
                    downlink_send(rnti, mac::LCID_APP_DTCH,
                                  pdcp::tx(rlc::tm_tx(pdu)));
                }
                break;
            }
            default:
                break;
        }
    }
}

// ---- M14: mobility ----------------------------------------------------------

void BsNode::handle_ccch_sdu(uint16_t rnti, const std::vector<uint8_t>& sdu) {
    auto msg = rrc::RrcMessage::decode(sdu);
    switch (msg.msg_type) {
        case rrc::RrcMessageType::MEAS_REPORT: {
            // [n:1]{[cell_id:2][strength:1]}; strength = SIB RX count.
            // Policy: hand over only when the SERVING cell went missing from
            // the report (radio condition degraded) while another cell is
            // audible — co-visible neighbours alone do not trigger churn.
            if (msg.value.empty()) break;
            const size_t n = msg.value[0];
            bool serving_audible = false;
            uint16_t best_cell = 0;
            uint8_t best_strength = 0;
            for (size_t i = 0; i < n && 1 + 3 * i + 2 < msg.value.size(); ++i) {
                uint16_t cell = static_cast<uint16_t>(
                    msg.value[1 + 3 * i] | (msg.value[2 + 3 * i] << 8));
                uint8_t strength = msg.value[3 + 3 * i];
                if (cell == config_.cell_id) {
                    serving_audible = true;
                } else if (strength > best_strength) {
                    best_strength = strength;
                    best_cell = cell;
                }
            }
            if (!serving_audible && best_cell != 0 &&
                config_.auto_handover && ho_coordinator_) {
                request_handover(rnti, best_cell);
            }
            break;
        }
        case rrc::RrcMessageType::HO_COMPLETE: {
            if (msg.value.size() < 2) break;
            uint16_t new_crnti = static_cast<uint16_t>(msg.value[0] |
                                                       (msg.value[1] << 8));
            if (ho_prepared_.count(new_crnti)) {
                LOG_INFO(ev::HO_COMPLETE_RX,
                         {{"cell", std::to_string(config_.cell_id)},
                          {"rnti", std::to_string(new_crnti)}});
            }
            // As the source: release the old context once the UE confirms it
            // landed on the target cell.
            for (auto it = initiated_ho_.begin(); it != initiated_ho_.end();
                 ++it) {
                if (it->second.new_crnti == new_crnti) {
                    uint16_t old_rnti = it->first;
                    uint32_t tmsi = tmsi_for_crnti(old_rnti);
                    if (tmsi != 0) {
                        nas_bs_.release_ue(tmsi);
                        tmsi_to_crnti_.erase(tmsi);
                    }
                    rrc_bs_.release_context(old_rnti);
                    flows_.erase(old_rnti);
                    initiated_ho_.erase(it);
                    break;
                }
            }
            (void)rnti;
            break;
        }
        default:
            // SETUP_*, RELEASE and REESTABLISHMENT_* stay in the RRC layer;
            // the reest callback (wired in the ctor) migrates our flows.
            rrc_bs_.handle_message(rnti, sdu);
            break;
    }
}

uint32_t BsNode::tmsi_for_crnti(uint16_t crnti) const {
    for (const auto& [tmsi, c] : tmsi_to_crnti_) {
        if (c == crnti) return tmsi;
    }
    return 0;
}

void BsNode::request_handover(uint16_t crnti, uint16_t target_cell_id) {
    auto fit = flows_.find(crnti);
    if (fit == flows_.end() || !ho_coordinator_) return;

    HoContext ctx;
    ctx.tmsi = tmsi_for_crnti(crnti);
    ctx.sec_on = fit->second.sec_on;
    ctx.up_key = fit->second.up_key;
    const auto* ue = nas_bs_.find_ue(ctx.tmsi); // IMSI lookup, best effort
    if (ue) ctx.imsi = ue->imsi;

    auto new_crnti = ho_coordinator_(target_cell_id, ctx);
    if (!new_crnti.has_value()) {
        LOG_WARN(ev::HO_TRIGGERED,
                 {{"from_cell", std::to_string(config_.cell_id)},
                  {"to_cell", std::to_string(target_cell_id)},
                  {"result", "refused"}});
        return;
    }

    LOG_INFO(ev::HO_TRIGGERED,
             {{"from_cell", std::to_string(config_.cell_id)},
              {"to_cell", std::to_string(target_cell_id)}});
    LOG_INFO(ev::HO_COMMAND_TX,
             {{"cell", std::to_string(target_cell_id)},
              {"rnti", std::to_string(*new_crnti)}});

    rrc::RrcMessage cmd;
    cmd.msg_type = rrc::RrcMessageType::HO_COMMAND;
    cmd.value = {static_cast<uint8_t>(target_cell_id & 0xFF),
                 static_cast<uint8_t>((target_cell_id >> 8) & 0xFF),
                 static_cast<uint8_t>(*new_crnti & 0xFF),
                 static_cast<uint8_t>((*new_crnti >> 8) & 0xFF)};
    downlink_send(crnti, mac::LCID_CCCH, cmd.encode());
    initiated_ho_[crnti] = {target_cell_id, *new_crnti};
}

std::optional<uint16_t> BsNode::prepare_handover(const HoContext& ctx) {
    uint16_t new_crnti = next_ho_crnti_++;
    rrc_bs_.admit_connected(new_crnti);
    auto& f = flow(new_crnti);
    f.up_key = ctx.up_key;
    f.sec_on = ctx.sec_on;
    if (ctx.tmsi != 0) tmsi_to_crnti_[ctx.tmsi] = new_crnti;
    if (!ctx.imsi.empty()) nas_bs_.adopt_ue(ctx.tmsi, ctx.imsi, ctx.up_key);
    ho_prepared_[new_crnti] = ctx;
    return new_crnti;
}

void BsNode::downlink_send(uint16_t rnti, uint8_t lcid,
                           const std::vector<uint8_t>& sdu_in) {
    std::vector<uint8_t> sdu = sdu_in;
    auto& f = flow(rnti);
    const bool ciphered =
        f.sec_on && (lcid == mac::LCID_NAS_DCCH || lcid == mac::LCID_APP_DTCH);
    if (ciphered) {
        sdu = pdcp::protect(f.up_key, f.dl_seq++, sdu);
    }
    std::vector<uint8_t> pdu = mac::build_pdu({{lcid, sdu}});
    trace_pdu("MAC", "TX", ciphered ? "dl enc" : "dl enqueue", pdu);
    if (f.queue.size() >= config_.max_dl_queue_per_ue) {
        f.queue.pop_front(); // backpressure: shed the oldest
        ++f.dropped;
    }
    f.queue.push_back(std::move(pdu));
}

void BsNode::downlink_raw(uint16_t rnti, uint8_t lcid,
                          const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu = mac::build_pdu({{lcid, sdu}});
    send_frame(AirFrameType::DATA, rnti, pdu);
}

void BsNode::send_frame(AirFrameType type, uint16_t rnti,
                        const std::vector<uint8_t>& payload) {
    if (!air_send_) return;
    AirFrame frame;
    frame.type = type;
    frame.rnti = rnti;
    frame.payload = payload;
    air_send_(pack_air_bits(encode_frame(frame)));
}

}

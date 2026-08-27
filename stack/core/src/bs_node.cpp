#include "core/bs_node.h"
#include "core/pdu_trace.h"
#include "mac/mac_pdu.h"
#include "nas/nas_messages.h"
#include "pdcp/pdcp_entity.h"
#include "phy/qam.h"
#include "rlc/rlc_tm.h"
#include "rrc/rrc_messages.h"
#include <algorithm>

namespace core {

BsNode::BsNode(const BsNodeConfig& config) : config_(config) {
    rrc_bs_.set_cell_identity(config_.cell_id, config_.plmn_id, config_.tac);
    // M22: cell-scoped identifier spaces (shared medium with a second cell).
    rrc_bs_.set_crnti_base(config_.crnti_base);
    rach_bs_.set_cell_id(config_.cell_id);
    rach_bs_.set_crnti_base(config_.crnti_base);
    next_ho_crnti_ = config_.crnti_base + 0x1000;

    // M14: a successful re-establishment migrated the RRC context to a new
    // C-RNTI — move the data-path state (flow, security, AM entities) with it.
    // M20: the same callback restores a resumed UE's data path (queues
    // intact, suspension cleared).
    rrc_bs_.set_reest_callback([this](uint16_t old_rnti, uint16_t new_rnti) {
        auto it = flows_.find(old_rnti);
        if (it != flows_.end()) {
            auto f = std::move(it->second);
            f.suspended = false; // M20: resumed — schedule again
            f.harq_tx.reset();
            f.harq_rx.reset();
            f.last_activity_ms = now_ms_; // fresh inactivity window
            flows_.erase(it);
            flows_[new_rnti] = std::move(f);
        }
        for (auto& [tmsi, crnti] : tmsi_to_crnti_) {
            if (crnti == old_rnti) { crnti = new_rnti; break; }
        }
    });

    // M20: a UE entered RRC_INACTIVE — park its data path (context, keys
    // and bearer queues are kept for the resume).
    rrc_bs_.set_suspend_callback([this](uint16_t rnti) {
        auto& f = flow(rnti);
        f.suspended = true;
        f.harq_tx.reset();
        f.harq_rx.reset();
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

        case cn::MsgType::HO_COMMAND: {
            // AMF-arbitrated handover: {tmsi:4}{tgt:2} ++ ctx blob
            // (sec_on:1, key:32, imsi_len:1, imsi). Only the named target
            // acts; other gNBs sharing the link ignore it.
            if (msg.value.size() < 6) break;
            const uint32_t ho_tmsi = cn::get32(msg.value, 0);
            const uint16_t target_cell = cn::get16(msg.value, 4);
            if (target_cell != config_.cell_id) break;
            BsNode::HoContext ctx;
            ctx.tmsi = ho_tmsi;
            const std::vector<uint8_t>& v = msg.value;
            size_t off = 6;
            if (v.size() < off + 1 + crypto::kKey256Size + 1) break;
            ctx.sec_on = v[off] != 0;
            ++off;
            std::copy(v.begin() + static_cast<long>(off),
                      v.begin() + static_cast<long>(off + crypto::kKey256Size),
                      ctx.up_key.begin());
            off += crypto::kKey256Size;
            const uint8_t imsi_len = v[off];
            ++off;
            if (v.size() < off + imsi_len) break;
            ctx.imsi.assign(v.begin() + static_cast<long>(off),
                            v.begin() + static_cast<long>(off + imsi_len));
            auto new_crnti = prepare_handover(ctx);
            if (new_crnti && cn_amf_) {
                // Tell the AMF (and thus the source gNB) the allocation.
                cn::CnMessage note;
                note.msg_type = cn::MsgType::HO_NOTIFY;
                cn::put32(note.value, ctx.tmsi);
                cn::put16(note.value, *new_crnti);
                cn_amf_->send(note);
            }
            break;
        }

        case cn::MsgType::HO_PREPARED: {
            // {tmsi:4}{tgt:2}{new_rnti:2} — the source completes the UE
            // notification once the target has allocated a C-RNTI.
            if (msg.value.size() < 8) break;
            const uint32_t tmsi = cn::get32(msg.value, 0);
            const uint16_t tgt = cn::get16(msg.value, 4);
            const uint16_t new_crnti = cn::get16(msg.value, 6);
            for (auto& [src_rnti, ho] : initiated_ho_) {
                if (ho.target_cell == tgt &&
                    tmsi_for_crnti(src_rnti) == tmsi) {
                    ho.new_crnti = new_crnti;
                    LOG_INFO(ev::HO_COMMAND_TX,
                             {{"cell", std::to_string(tgt)},
                              {"rnti", std::to_string(new_crnti)}});
                    rrc::RrcMessage cmd;
                    cmd.msg_type = rrc::RrcMessageType::HO_COMMAND;
                    cmd.value = {
                        static_cast<uint8_t>(tgt & 0xFF),
                        static_cast<uint8_t>((tgt >> 8) & 0xFF),
                        static_cast<uint8_t>(new_crnti & 0xFF),
                        static_cast<uint8_t>((new_crnti >> 8) & 0xFF)};
                    downlink_send(src_rnti, mac::LCID_CCCH, cmd.encode());
                    break;
                }
            }
            break;
        }

        case cn::MsgType::DL_DATA: {
            // UPF-routed downlink user-plane data for one of our UEs.
            if (msg.value.size() < 6) break;
            const uint16_t rnti = cn::get16(msg.value, 4);
            std::vector<uint8_t> data(msg.value.begin() + 6, msg.value.end());
            auto& f = flow(rnti);
            auto& be = f.bearers.am_of(Qci::BEST_EFFORT);
            for (const auto& pdu : be.tx.tx(now_ms_, data)) {
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
    for (uint16_t r : pending_flow_erase_) flows_.erase(r); // M22 deferred
    pending_flow_erase_.clear();
    timers_.tick(now_ms_);
    flush_forward_log();
    sweep_tpc();
    sweep_inactive();
    schedule_downlink();
}

// M20: suspend connected UEs whose user plane went quiet. Any non-ACK
// uplink refreshes last_activity_ms, so an active call or media stream is
// never suspended.
void BsNode::sweep_inactive() {
    if (config_.inactive_ms == 0) return;
    for (auto it = flows_.begin(); it != flows_.end();) {
        auto& [rnti, f] = *it;
        // Context expiry: a UE that never resumes within 5x the timer is
        // garbage-collected; a later resume attempt gets RESUME_FAILURE
        // and falls back to full attach (as NR does).
        if (f.suspended &&
            now_ms_ - f.last_activity_ms > 5 * config_.inactive_ms) {
            rrc_bs_.release_context(rnti);
            const uint32_t tmsi = tmsi_for_crnti(rnti);
            if (tmsi != 0) tmsi_to_crnti_.erase(tmsi);
            log_bs_bearer_teardowns(rnti, f);
            LOG_INFO(ev::RRC_UE_RELEASED, {{"c_rnti", std::to_string(rnti)},
                                           {"reason", "inactive_expiry"}});
            it = flows_.erase(it);
            continue;
        }
        if (!f.suspended && f.last_activity_ms != 0 &&
            rrc_bs_.is_ue_connected(rnti) &&
            now_ms_ - f.last_activity_ms > config_.inactive_ms) {
            rrc_bs_.suspend_context(rnti);
        }
        ++it;
    }
}

bool BsNode::flow_suspended(uint16_t rnti) const {
    auto it = flows_.find(rnti);
    return it != flows_.end() && it->second.suspended;
}

BsNode::DlFlow& BsNode::flow(uint16_t rnti) {
    auto it = flows_.find(rnti);
    if (it == flows_.end()) {
        it = flows_.emplace(rnti, DlFlow{}).first;
    }
    return it->second;
}

// Fair full pass: every connected flow may fill its HARQ pipe with new
// transport blocks per tick (the in-flight window, not the tick rate, is
// the real throttle — one block per tick starved the downlink at media
// rates when ticks slowed under load). M17: within a flow the QoS bearers
// drain by strict priority (ctrl > sig > voice > video > best-effort with
// a BE min-share guard); flows are still served round-robin. Uplink stays
// grant-free (configured grants in 3GPP terms).
void BsNode::schedule_downlink() {
    pump_flows();
    for (auto& [rnti, f] : flows_) {
        while (!f.bearers.empty() &&
               f.harq_tx.in_flight() < f.harq_tx.num_processes()) {
            // M20: a suspended flow's app bearers stay parked, but the
            // control queue still drains — the suspend RELEASE itself
            // (and later RRC control) must reach the UE.
            if (f.suspended && f.bearers.ctrl().empty()) break;
            AppPdu pdu = f.bearers.pop_next();
            // M17: PDCP ciphering at drain time so the shared COUNT matches
            // air order; the decision itself was captured at enqueue time.
            std::vector<uint8_t> sdu = pdu.bytes;
            if (pdu.cipher) {
                sdu = pdcp::protect(f.up_key, f.dl_seq++, sdu);
            }
            std::vector<uint8_t> tb = mac::build_pdu({{pdu.lcid, sdu}});
            trace_pdu("MAC", "TX", pdu.cipher ? "dl enc" : "dl scheduled", tb);
            if (auto e = f.harq_tx.send(tb)) {
                send_frame(AirFrameType::DATA, rnti, e->coded);
            } else {
                break; // harq pipe filled between the check and the send
            }
        }
    }
}

void BsNode::pump_flows() {
    for (auto& [rnti, f] : flows_) {
        // M20: HARQ retransmissions keep running for suspended flows — the
        // suspend RELEASE rides the control queue and must survive loss.
        for (auto& e : f.harq_tx.poll_timeouts(now_ms_)) {
            trace_pdu("HARQ", "TX", "retx", e.coded);
            send_frame(AirFrameType::DATA, rnti, e.coded);
        }
        // M13/M17: per-bearer liveness. AM bearers (sig, best-effort):
        // probe-retransmit + reorder resync. UM bearers (voice, video):
        // reorder timer only — no ARQ, losses are accepted.
        for (Qci qci : {Qci::SIG, Qci::BEST_EFFORT}) {
            auto& b = f.bearers.am_of(qci);
            for (const auto& pdu : b.tx.tick(now_ms_)) {
                downlink_send(rnti, lcid_of(qci), pdcp::tx(rlc::tm_tx(pdu)));
            }
            auto am_tick = b.rx.tick(now_ms_);
            for (const auto& data : am_tick.delivered) {
                handle_ul_app_sdu(rnti, data);
            }
            if (am_tick.status_needed) {
                auto status = b.rx.build_status();
                LOG_INFO(ev::RLC_AM_STATUS_TX,
                         {{"dir", "dl"}, {"nacks", std::to_string(status[2])}});
                downlink_send(rnti, status_lcid_of(qci), status);
            }
        }
        for (Qci qci : {Qci::VOICE, Qci::VIDEO}) {
            auto& b = f.bearers.um_of(qci);
            b.rx.tick(now_ms_);
            for (const auto& data : b.rx.poll()) {
                handle_ul_app_sdu(rnti, data);
            }
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
    pending_ul_snr_ = -100.f; // consumed (or discarded) with this burst
}

void BsNode::on_air_bits_with_metrics(const std::vector<uint8_t>& bits,
                                      float snr_db, float /*pwr_dbm*/) {
    pending_ul_snr_ = snr_db; // consumed by handle_dl_data for this burst
    on_air_bits(bits);
}

int BsNode::dl_mcs(uint16_t rnti) const {
    auto it = flows_.find(rnti);
    return it == flows_.end() ? 0 : it->second.mcs;
}

int BsNode::flow_cqi(uint16_t rnti) const {
    auto it = flows_.find(rnti);
    return it == flows_.end() ? -1 : it->second.cqi;
}

// M19: per-burst MCS. Only dedicated DATA bursts to a flow with a CQI
// report adapt; broadcast/RACH/control stays on robust QPSK (a UE without
// context — or an unsynced neighbour — must still decode).
int BsNode::mcs_for_frame(const AirFrame& frame) {
    if (frame.type != AirFrameType::DATA || frame.rnti == mac::RNTI_BROADCAST) {
        return 0;
    }
    auto it = flows_.find(frame.rnti);
    return it == flows_.end() ? 0 : it->second.mcs;
}

// M19: closed-loop UL power control — steer each flow's UL arrival SNR to
// the target with +/-1 dB TPC commands at most every 500 ms. Target 23 dB:
// the receiver's measured QPSK floor for ~100% burst decode is ~20 dB
// (bench over tools/channel absolute noise), so 23 leaves 3 dB of margin.
void BsNode::sweep_tpc() {
    constexpr float kTargetSnrDb = 23.f;
    for (auto& [rnti, f] : flows_) {
        if (f.suspended) continue; // M20: parked until resume
        if (f.ul_snr_ewma < -90.f) continue; // no UL measurements yet
        if (now_ms_ - f.last_tpc_ms < 500) continue;
        const float err = kTargetSnrDb - f.ul_snr_ewma;
        int cmd = 0;
        if (err > 2.f) cmd = 1;
        else if (err < -2.f) cmd = -1;
        if (cmd == 0) continue;
        f.last_tpc_ms = now_ms_;
        downlink_send(rnti, mac::LCID_TPC,
                      {static_cast<uint8_t>(static_cast<int8_t>(cmd))});
    }
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

// One reassembled uplink app-bearer SDU (from AM rx or from a reorder-
// timeout resync): forward U2U media into the peer UE's downlink flow,
// tunnel to the UPF in the separated-core topology, else legacy echo.
void BsNode::handle_ul_app_sdu(uint16_t rnti,
                               const std::vector<uint8_t>& data) {
    trace_pdu("APP", "RX", "ping", data);
    if (cn_upf_) {
        // M15: uplink bearer terminates at the UPF anchor.
        LOG_INFO(ev::APP_ECHO_TX,
                 {{"len", std::to_string(data.size())}, {"upf", "1"}});
        cn_send_uplink_data(tmsi_for_crnti(rnti), rnti, data);
        return;
    }
    // M16: U2U-framed SDUs are forwarded into the peer UE's downlink flow;
    // unknown/absent dst falls back to echo. M17: the downlink LCID mirrors
    // the service class, so the peer's dedicated bearer carries the media.
    app::U2uPacket pkt;
    if (app::decode_u2u(data, pkt)) {
        // M18: conference media is addressed to the bridge (empty dst) —
        // fan it out to every other conference member. Acks keep their
        // unicast dst (the original sender) and take the normal path.
        if (pkt.conf_id != 0 && !pkt.ack &&
            pkt.kind == app::MediaKind::VOICE) {
            bridge_conf_media(rnti, pkt);
            return;
        }
        // M18: splice conference membership from the signaling we forward.
        if (pkt.kind == app::MediaKind::SIG) {
            app::SigMessage msg;
            if (app::decode_sig(pkt.payload, msg) && msg.conf_id != 0) {
                snoop_conf_sig(pkt.src_imsi, pkt.dst_imsi, msg);
            }
        }
        if (forward_u2u_dl(pkt, data, rnti)) {
            return;
        }
        // M22: the destination may have moved to the peer cell — hand the
        // SDU over Xn (the peer drops it silently if it is unknown there).
        if (xn_) {
            xn_forward_data(pkt.dst_imsi, data);
            log_forward(pkt, data.size());
            return;
        }
    }
    LOG_INFO(ev::APP_ECHO_TX, {{"len", std::to_string(data.size())}});
    auto& f = flow(rnti);
    auto& be = f.bearers.am_of(Qci::BEST_EFFORT);
    for (const auto& pdu : be.tx.tx(now_ms_, data)) {
        downlink_send(rnti, mac::LCID_APP_DTCH, pdcp::tx(rlc::tm_tx(pdu)));
    }
}

void BsNode::handle_dl_data(uint16_t rnti, const std::vector<uint8_t>& payload) {
    // M19: the burst's UL SNR feeds this flow's TPC control loop.
    if (pending_ul_snr_ > -90.f) {
        auto& mf = flow(rnti);
        constexpr float kAlpha = 0.3f;
        mf.ul_snr_ewma = mf.ul_snr_ewma < -90.f
                             ? pending_ul_snr_
                             : mf.ul_snr_ewma +
                                   kAlpha * (pending_ul_snr_ - mf.ul_snr_ewma);
    }
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
        // M20: user-plane activity for the RRC_INACTIVE timer. Background
        // protocol chatter does NOT count: MEAS_REPORTs (CCCH), CQI CEs
        // and HARQ-ACKs flow even from an idle UE and would pin it
        // connected forever. The same test drives the desync recovery:
        // real content from a UE we suspended proves the suspend RELEASE
        // never landed (a bare HARQ-ACK must NOT un-park — the UE acks
        // the suspend RELEASE at MAC level before going inactive).
        if (lcid == mac::LCID_NAS_DCCH || lcid == mac::LCID_APP_DTCH ||
            lcid == mac::LCID_APP_SIG || lcid == mac::LCID_APP_VOICE ||
            lcid == mac::LCID_APP_VIDEO) {
            auto& df = flow(rnti);
            df.last_activity_ms = now_ms_;
            if (df.suspended) {
                df.suspended = false;
                rrc_bs_.reactivate_context(rnti);
            }
        }
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
                        if (const auto* ctx = nas_bs_.find_ue(tmsi)) {
                            purge_conf_member(ctx->imsi); // M18
                        }
                        log_bs_bearer_teardowns(rnti, flow(rnti));
                        flows_.erase(rnti);
                    }
                    break;
                }
                auto decoded = nas::NasMessage::decode(nas_pdu);
                bool detach = decoded.msg_type == nas::NasMessageType::DETACH;
                nas_bs_.handle_message(0, nas_pdu);
                if (detach) {
                    const uint32_t tmsi = tmsi_for_crnti(rnti);
                    for (auto it = tmsi_to_crnti_.begin();
                         it != tmsi_to_crnti_.end();) {
                        if (it->second == rnti) it = tmsi_to_crnti_.erase(it);
                        else ++it;
                    }
                    if (const auto* ctx = nas_bs_.find_ue(tmsi)) {
                        purge_conf_member(ctx->imsi); // M18
                    }
                    log_bs_bearer_teardowns(rnti, flow(rnti));
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
            case mac::LCID_CQI_REPORT: {
                // M19: UE's serving-cell CQI (1..15) drives this flow's DL
                // MCS. Events are change-only — the CE itself can repeat.
                if (sdu.empty()) break;
                auto& cf = flow(rnti);
                const int cqi = sdu[0] & 0x1F;
                cf.cqi = cqi;
                if (cqi != cf.last_cqi_logged) {
                    cf.last_cqi_logged = cqi;
                    LOG_INFO(ev::CQI_REPORT,
                             {{"c_rnti", std::to_string(rnti)},
                              {"cqi", std::to_string(cqi)},
                              {"snr_db", std::to_string(
                                             static_cast<int>(cf.ul_snr_ewma))}});
                }
                // Ladder: qpsk < cqi 14 | 16qam >= 14 (SNR ~26 dB). Floors
                // come from the measured decode curve (docs/m19_plan.md):
                // 16QAM only beats QPSK where its bursts actually survive
                // (~26 dB+). 64QAM is implemented but NEVER selected — its
                // practical floor is beyond any link we simulate.
                const int mcs = cqi >= 14 ? 1 : 0;
                if (mcs != cf.mcs) {
                    cf.mcs = mcs;
                    LOG_INFO(ev::MCS_CHANGE,
                             {{"c_rnti", std::to_string(rnti)},
                              {"mcs", phy::mcs_name(
                                          static_cast<phy::Mcs>(mcs))},
                              {"direction", "dl"}});
                }
                break;
            }
            case mac::LCID_APP_DTCH:
            case mac::LCID_APP_SIG:
            case mac::LCID_APP_VOICE:
            case mac::LCID_APP_VIDEO: {
                // M17: route the SDU to its QoS bearer's uplink AM entity.
                const Qci qci = *qci_of_lcid(lcid);
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
                // M13/M17: RLC data PDU for this bearer's uplink entity
                // (AM for sig/best-effort, UM for voice/video).
                ensure_bs_bearer(rnti, rxf, qci);
                if (qci == Qci::SIG || qci == Qci::BEST_EFFORT) {
                    auto& b = rxf.bearers.am_of(qci);
                    auto am_out = b.rx.rx(am_pdu);
                    for (const auto& data : am_out.delivered) {
                        handle_ul_app_sdu(rnti, data);
                    }
                    if (am_out.status_needed) {
                        auto status = b.rx.build_status();
                        LOG_INFO(ev::RLC_AM_STATUS_TX,
                                 {{"dir", "dl"}, {"nacks", std::to_string(status[2])}});
                        downlink_send(rnti, status_lcid_of(qci), status);
                    }
                } else {
                    auto& b = rxf.bearers.um_of(qci);
                    b.rx.rx(now_ms_, am_pdu);
                    for (const auto& data : b.rx.poll()) {
                        handle_ul_app_sdu(rnti, data);
                    }
                }
                break;
            }
            case mac::LCID_RLC_STATUS:
            case mac::LCID_RLC_STATUS_SIG: {
                // UE reports holes in one downlink AM stream: retransmit on
                // the matching bearer.
                const Qci qci = *qci_of_status_lcid(lcid);
                auto& sf = flow(rnti);
                auto& b = sf.bearers.am_of(qci);
                for (const auto& pdu : b.tx.on_status(now_ms_, sdu)) {
                    downlink_send(rnti, lcid_of(qci),
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

// Source-side context teardown once the UE has landed on the target cell
// (registration, flows, RRC context). Used by the legacy HO_COMPLETE path
// and by the M22 XN_HO_COMPLETE notification — the HO_COMPLETE frame
// itself never reaches the source through the RNTI ownership gate.
void BsNode::release_ho_source(uint16_t old_rnti) {
    const uint32_t tmsi = tmsi_for_crnti(old_rnti);
    if (tmsi != 0) {
        nas_bs_.release_ue(tmsi);
        tmsi_to_crnti_.erase(tmsi);
        if (cn_amf_) {
            cn::CnMessage rel;
            rel.msg_type = cn::MsgType::UE_CTX_RELEASE;
            cn::put32(rel.value, tmsi);
            cn_amf_->send(rel);
        }
    }
    rrc_bs_.release_context(old_rnti);
    log_bs_bearer_teardowns(old_rnti, flow(old_rnti));
    // Defer the actual erase: an XN_HO_COMPLETE can arrive reentrantly
    // (synchronous in-memory Xn) while schedule_downlink iterates flows_.
    pending_flow_erase_.push_back(old_rnti);
}

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
            if (!serving_audible && best_cell != 0 && config_.auto_handover &&
                (ho_coordinator_ || core_separated() || xn_)) {
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
                // M22: the UE landed on this cell — handover complete.
                const auto& pctx = ho_prepared_[new_crnti];
                if (pctx.from_cell != 0) {
                    LOG_INFO(ev::HANDOVER_DONE,
                             {{"imsi", pctx.imsi},
                              {"from", std::to_string(pctx.from_cell)},
                              {"to", std::to_string(config_.cell_id)},
                              {"path", "ho"}});
                }
            }
            // As the source: release the old context once the UE confirms
            // it landed on the target cell.
            for (auto it = initiated_ho_.begin(); it != initiated_ho_.end();
                 ++it) {
                if (it->second.new_crnti == new_crnti && new_crnti != 0) {
                    release_ho_source(it->first);
                    initiated_ho_.erase(it);
                    break;
                }
            }
            // M22: tell the source (over Xn) that the UE landed — its own
            // copy of HO_COMPLETE is eaten by the RNTI ownership gate.
            if (ho_prepared_.count(new_crnti) && xn_) {
                cn::CnMessage done;
                done.msg_type = cn::MsgType::XN_HO_COMPLETE;
                cn::put32(done.value, ho_prepared_[new_crnti].tmsi);
                cn::put16(done.value, new_crnti);
                xn_->send(done);
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

// ---- M16: UE-to-UE forwarding ------------------------------------------------

// ---- M18: 3-party conference (audio bridge) ------------------------------------

// Fan one conference media packet out to every member except the sender.
// Fan-out copies are re-encoded per receiver (dst = receiver IMSI; src,
// seq and conf_id preserved) so each party accounts media per sender.
void BsNode::bridge_conf_media(uint16_t src_rnti, const app::U2uPacket& pkt) {
    auto it = confs_.find(pkt.conf_id);
    if (it == confs_.end()) return; // no such conference: drop
    for (const std::string& imsi : it->second.members) {
        if (imsi == pkt.src_imsi) continue;
        const uint16_t dst_rnti = crnti_for_imsi(imsi);
        if (dst_rnti == 0 || dst_rnti == src_rnti || !flows_.count(dst_rnti)) {
            continue;
        }
        app::U2uPacket copy = pkt;
        copy.dst_imsi = imsi;
        const std::vector<uint8_t> bytes = app::encode_u2u(copy);
        auto& df = flow(dst_rnti);
        if (df.suspended) page(imsi); // M20: wake the member for its media
        ensure_bs_bearer(dst_rnti, df, Qci::VOICE);
        auto& b = df.bearers.um_of(Qci::VOICE);
        for (const auto& fpdu : b.tx.tx(bytes)) {
            downlink_send(dst_rnti, mac::LCID_APP_VOICE,
                          pdcp::tx(rlc::tm_tx(fpdu)));
        }
        log_forward(copy, bytes.size());
    }
}

namespace {
bool conf_contains(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}
}

void BsNode::snoop_conf_sig(const std::string& src, const std::string& dst,
                            const app::SigMessage& msg) {
    const uint32_t id = msg.conf_id;
    if (msg.method == app::SigMethod::INVITE) {
        auto& c = confs_[id];
        if (c.host.empty()) {
            c.host = src;
            c.members.push_back(src);
            LOG_INFO(ev::CONF_START,
                     {{"host", src}, {"conf_id", std::to_string(id)}});
        }
        if (src == c.host && !conf_contains(c.invited, dst)) {
            c.invited.push_back(dst);
        }
        return;
    }
    auto it = confs_.find(id);
    if (it == confs_.end()) return;
    auto& c = it->second;
    switch (msg.method) {
        case app::SigMethod::OK_200: // party -> host: joined
            if (!conf_contains(c.members, src)) {
                c.members.push_back(src);
                if (conf_contains(c.invited, src) &&
                    !conf_contains(c.resolved, src)) {
                    c.resolved.push_back(src);
                }
                c.ever_multi = c.ever_multi || c.members.size() >= 2;
                LOG_INFO(ev::CONF_JOIN,
                         {{"conf_id", std::to_string(id)}, {"imsi", src}});
            }
            return;
        case app::SigMethod::BYE:
            if (conf_contains(c.members, src)) {
                conf_leave(id, src, "hangup");
            }
            return;
        case app::SigMethod::BUSY_486:
        case app::SigMethod::DECLINE_603: {
            // The invited party refused (never joined).
            const char* reason = msg.method == app::SigMethod::BUSY_486
                                     ? "busy"
                                     : "decline";
            if (conf_contains(c.invited, src) &&
                !conf_contains(c.resolved, src)) {
                c.resolved.push_back(src);
                LOG_INFO(ev::CONF_LEAVE,
                         {{"conf_id", std::to_string(id)}, {"imsi", src},
                          {"reason", reason}});
                conf_maybe_end(id);
            }
            return;
        }
        case app::SigMethod::CANCEL: // host gave up on a ringing party
            if (src == c.host && conf_contains(c.invited, dst) &&
                !conf_contains(c.resolved, dst)) {
                c.resolved.push_back(dst);
                LOG_INFO(ev::CONF_LEAVE,
                         {{"conf_id", std::to_string(id)}, {"imsi", dst},
                          {"reason", "cancel"}});
                conf_maybe_end(id);
            }
            return;
        default:
            return; // 180, ACK, OK_BYE: no membership change
    }
}

// Remove `imsi` from one conference (BYE hangup or detach purge). The host
// leaving ends the whole conference; a party leaving shrinks it.
void BsNode::conf_leave(uint32_t conf_id, const std::string& imsi,
                        const char* reason) {
    auto it = confs_.find(conf_id);
    if (it == confs_.end()) return;
    auto& c = it->second;
    LOG_INFO(ev::CONF_LEAVE,
             {{"conf_id", std::to_string(conf_id)}, {"imsi", imsi},
              {"reason", reason}});
    for (auto m = c.members.begin(); m != c.members.end();) {
        if (*m == imsi) m = c.members.erase(m);
        else ++m;
    }
    if (imsi == c.host) {
        LOG_INFO(ev::CONF_END,
                 {{"conf_id", std::to_string(conf_id)}, {"reason", "host"}});
        confs_.erase(it);
        return;
    }
    conf_maybe_end(conf_id);
}

// A conference closes once every invitation is resolved and fewer than two
// members remain ("empty" when it had been multi-party, else "no-parties").
void BsNode::conf_maybe_end(uint32_t conf_id) {
    auto it = confs_.find(conf_id);
    if (it == confs_.end()) return;
    const auto& c = it->second;
    if (c.members.size() >= 2 || c.invited.size() != c.resolved.size()) {
        return;
    }
    LOG_INFO(ev::CONF_END,
             {{"conf_id", std::to_string(conf_id)},
              {"reason", c.ever_multi ? "empty" : "no-parties"}});
    confs_.erase(it);
}

// Backstop for a UE that vanished without a BYE (link death, detach race):
// drop it from every conference it belonged to.
void BsNode::purge_conf_member(const std::string& imsi) {
    std::vector<uint32_t> ids;
    for (const auto& [id, c] : confs_) ids.push_back(id);
    for (const uint32_t id : ids) {
        auto it = confs_.find(id);
        if (it == confs_.end()) continue;
        if (it->second.host == imsi ||
            conf_contains(it->second.members, imsi)) {
            conf_leave(id, imsi, "detach");
        }
    }
}

size_t BsNode::conf_member_count(uint32_t conf_id) const {
    auto it = confs_.find(conf_id);
    return it == confs_.end() ? 0 : it->second.members.size();
}

// ---- M22: Xn interface to the peer gNB ---------------------------------------

void BsNode::attach_xn(cn::CnLink* link, uint16_t peer_cell) {
    xn_ = link;
    xn_peer_cell_ = peer_cell;
    xn_->set_handler([this](const cn::CnMessage& m) { handle_xn_message(m); });
}

void BsNode::handle_xn_message(const cn::CnMessage& msg) {
    switch (msg.msg_type) {
        case cn::MsgType::XN_HO_PREPARE: {
            // {tmsi:4}{from:2}{to:2}{sec:1}{key:32}{imsi_len:1}{imsi}
            if (msg.value.size() < 41) break;
            HoContext ctx;
            ctx.tmsi = cn::get32(msg.value, 0);
            ctx.from_cell = cn::get16(msg.value, 4);
            const uint16_t to = cn::get16(msg.value, 6);
            if (to != config_.cell_id) break; // not for this cell
            ctx.sec_on = msg.value[8] != 0;
            std::copy_n(msg.value.begin() + 9, 32, ctx.up_key.begin());
            const size_t ilen = msg.value[41];
            if (msg.value.size() >= 42 + ilen) {
                ctx.imsi.assign(msg.value.begin() + 42,
                                msg.value.begin() + 42 + ilen);
            }
            auto new_crnti = prepare_handover(ctx);
            cn::CnMessage ack;
            ack.msg_type = cn::MsgType::XN_HO_PREPARE_ACK;
            cn::put32(ack.value, ctx.tmsi);
            cn::put16(ack.value, new_crnti.value_or(0));
            xn_->send(ack);
            break;
        }
        case cn::MsgType::XN_HO_PREPARE_ACK: {
            // {tmsi:4}{new_rnti:2} — the target accepted; tell the UE.
            if (msg.value.size() < 6) break;
            const uint32_t tmsi = cn::get32(msg.value, 0);
            const uint16_t new_crnti = cn::get16(msg.value, 4);
            auto it = pending_xn_ho_.find(tmsi);
            if (it == pending_xn_ho_.end() || new_crnti == 0) break;
            const uint16_t src = it->second.source_crnti;
            const uint16_t target = it->second.target_cell;
            pending_xn_ho_.erase(it);
            LOG_INFO(ev::HO_COMMAND_TX,
                     {{"cell", std::to_string(target)},
                      {"rnti", std::to_string(new_crnti)}});
            rrc::RrcMessage cmd;
            cmd.msg_type = rrc::RrcMessageType::HO_COMMAND;
            cmd.value = {static_cast<uint8_t>(target & 0xFF),
                         static_cast<uint8_t>((target >> 8) & 0xFF),
                         static_cast<uint8_t>(new_crnti & 0xFF),
                         static_cast<uint8_t>((new_crnti >> 8) & 0xFF)};
            downlink_send(src, mac::LCID_CCCH, cmd.encode());
            initiated_ho_[src] = {target, new_crnti};
            break;
        }
        case cn::MsgType::XN_HO_COMPLETE: {
            // {tmsi:4}{new_rnti:2} — the UE confirmed on the target;
            // release the source-side context here.
            if (msg.value.size() < 6) break;
            const uint16_t new_crnti = cn::get16(msg.value, 4);
            for (auto it = initiated_ho_.begin(); it != initiated_ho_.end();
                 ++it) {
                if (it->second.new_crnti == new_crnti && new_crnti != 0) {
                    release_ho_source(it->first);
                    initiated_ho_.erase(it);
                    break;
                }
            }
            break;
        }
        case cn::MsgType::XN_FWD_DATA: {
            // {imsi_len:1}{imsi}{sdu} — a UE moved to this cell; deliver
            // its U2U downlink locally.
            if (msg.value.size() < 2) break;
            const size_t ilen = msg.value[0];
            if (msg.value.size() < 1 + ilen) break;
            std::vector<uint8_t> data(msg.value.begin() + 1 + ilen,
                                      msg.value.end());
            app::U2uPacket pkt;
            if (app::decode_u2u(data, pkt)) {
                forward_u2u_dl(pkt, data);
            }
            break;
        }
        default:
            break;
    }
}

// Deliver one U2U packet into the destination's downlink flow on THIS
// cell. Returns false when the IMSI is not registered here (or is the
// excluded sender) — the caller may then try the peer cell over Xn.
bool BsNode::forward_u2u_dl(const app::U2uPacket& pkt,
                            const std::vector<uint8_t>& data,
                            uint16_t exclude_rnti) {
    const uint16_t dst_rnti = crnti_for_imsi(pkt.dst_imsi);
    if (dst_rnti == 0 || dst_rnti == exclude_rnti || !flows_.count(dst_rnti)) {
        return false;
    }
    auto& df = flow(dst_rnti);
    if (df.suspended) {
        // M20: the SDU queues on the kept bearers; page the UE so it
        // resumes and drains them (e.g. an incoming SIP INVITE).
        page(pkt.dst_imsi);
    }
    const Qci qci = qci_of(pkt.kind);
    ensure_bs_bearer(dst_rnti, df, qci);
    if (qci == Qci::SIG || qci == Qci::BEST_EFFORT) {
        auto& b = df.bearers.am_of(qci);
        for (const auto& fpdu : b.tx.tx(now_ms_, data)) {
            downlink_send(dst_rnti, lcid_of(qci), pdcp::tx(rlc::tm_tx(fpdu)));
        }
    } else {
        auto& b = df.bearers.um_of(qci);
        for (const auto& fpdu : b.tx.tx(data)) {
            downlink_send(dst_rnti, lcid_of(qci), pdcp::tx(rlc::tm_tx(fpdu)));
        }
    }
    log_forward(pkt, data.size());
    return true;
}

void BsNode::xn_forward_data(const std::string& dst_imsi,
                             const std::vector<uint8_t>& sdu) {
    if (!xn_) return;
    cn::CnMessage m;
    m.msg_type = cn::MsgType::XN_FWD_DATA;
    m.value.push_back(static_cast<uint8_t>(dst_imsi.size()));
    m.value.insert(m.value.end(), dst_imsi.begin(), dst_imsi.end());
    m.value.insert(m.value.end(), sdu.begin(), sdu.end());
    xn_->send(m);
}

uint16_t BsNode::crnti_for_imsi(const std::string& imsi) const {
    const auto* ctx = nas_bs_.find_ue_by_imsi(imsi);
    if (ctx == nullptr) return 0;
    auto it = tmsi_to_crnti_.find(ctx->tmsi);
    return it == tmsi_to_crnti_.end() ? 0 : it->second;
}

void BsNode::log_forward(const app::U2uPacket& pkt, size_t bytes) {
    // M18: conference media forwards log with kind "conf" (they ride the
    // voice bearer; the conf_id travels in the packet itself).
    const char* kind =
        pkt.conf_id != 0 &&
                (pkt.kind == app::MediaKind::VOICE ||
                 pkt.kind == app::MediaKind::VIDEO)
            ? "conf"
            : app::media_kind_name(pkt.kind);
    if (pkt.kind == app::MediaKind::MSG || pkt.kind == app::MediaKind::SIG) {
        // One-shots (text, SIP-lite signaling) log immediately — they are
        // rare and individually interesting.
        LOG_INFO(ev::APP_FORWARD,
                 {{"src", pkt.src_imsi}, {"dst", pkt.dst_imsi},
                  {"kind", kind}, {"bytes", std::to_string(bytes)},
                  {"count", "1"}});
        return;
    }
    // Streams would emit 30-100 events/s per call; aggregate per
    // (src,dst,kind) over a 1 s window instead. First packet opens a window,
    // the flush happens from log_forward/tick once the window expires.
    for (auto& agg : fwd_agg_) {
        if (agg.src == pkt.src_imsi && agg.dst == pkt.dst_imsi &&
            agg.kind == kind) {
            ++agg.count;
            agg.bytes += bytes;
            return;
        }
    }
    FwdAgg agg;
    agg.src = pkt.src_imsi;
    agg.dst = pkt.dst_imsi;
    agg.kind = kind;
    agg.count = 1;
    agg.bytes = bytes;
    agg.window_start_ms = now_ms_;
    fwd_agg_.push_back(std::move(agg));
}

void BsNode::flush_forward_log() {
    constexpr uint32_t kFwdWindowMs = 1000;
    for (auto it = fwd_agg_.begin(); it != fwd_agg_.end();) {
        if (static_cast<int32_t>(now_ms_ - it->window_start_ms) >=
            static_cast<int32_t>(kFwdWindowMs)) {
            LOG_INFO(ev::APP_FORWARD,
                     {{"src", it->src}, {"dst", it->dst},
                      {"kind", it->kind}, {"bytes", std::to_string(it->bytes)},
                      {"count", std::to_string(it->count)}});
            it = fwd_agg_.erase(it);
        } else {
            ++it;
        }
    }
}

void BsNode::request_handover(uint16_t crnti, uint16_t target_cell_id) {
    auto fit = flows_.find(crnti);
    if (fit == flows_.end()) return;
    if (!ho_coordinator_ && !core_separated() && !xn_) return;

    HoContext ctx;
    ctx.tmsi = tmsi_for_crnti(crnti);
    ctx.sec_on = fit->second.sec_on;
    ctx.up_key = fit->second.up_key;
    ctx.from_cell = config_.cell_id;
    const auto* ue = nas_bs_.find_ue(ctx.tmsi); // IMSI lookup, best effort
    if (ue) ctx.imsi = ue->imsi;

    // M22: Xn path (dual-BS demo) — prepare the target over the Xn link,
    // the HO_COMMAND follows its ACK asynchronously.
    if (xn_ && !ho_coordinator_ && !core_separated()) {
        if (initiated_ho_.count(crnti)) {
            return; // already in flight — a repeated meas report must not
                    // reset the pending completion
        }
        LOG_INFO(ev::HANDOVER_START,
                 {{"imsi", ctx.imsi},
                  {"from", std::to_string(config_.cell_id)},
                  {"to", std::to_string(target_cell_id)}});
        LOG_INFO(ev::HO_TRIGGERED,
                 {{"from_cell", std::to_string(config_.cell_id)},
                  {"to_cell", std::to_string(target_cell_id)},
                  {"via", "xn"}});
        cn::CnMessage m;
        m.msg_type = cn::MsgType::XN_HO_PREPARE;
        cn::put32(m.value, ctx.tmsi);
        cn::put16(m.value, config_.cell_id);
        cn::put16(m.value, target_cell_id);
        m.value.push_back(ctx.sec_on ? 1 : 0);
        m.value.insert(m.value.end(), ctx.up_key.begin(), ctx.up_key.end());
        m.value.push_back(static_cast<uint8_t>(ctx.imsi.size()));
        m.value.insert(m.value.end(), ctx.imsi.begin(), ctx.imsi.end());
        // Record the pending handover BEFORE sending: an in-memory Xn
        // carrier delivers the ACK synchronously inside send().
        pending_xn_ho_[ctx.tmsi] = {crnti, target_cell_id};
        initiated_ho_[crnti] = {target_cell_id, 0}; // rnti known at ACK
        xn_->send(m);
        return;
    }

    // M15: with a separated core the AMF arbitrates the handover — send
    // HO_REQUIRED and let it come back as HO_COMMAND on the target's link.
    if (core_separated() && cn_amf_ && !ho_coordinator_) {
        LOG_INFO(ev::HO_TRIGGERED,
                 {{"from_cell", std::to_string(config_.cell_id)},
                  {"to_cell", std::to_string(target_cell_id)},
                  {"via", "amf"}});
        cn::CnMessage m;
        m.msg_type = cn::MsgType::HO_REQUIRED;
        cn::put32(m.value, ctx.tmsi);
        cn::put16(m.value, target_cell_id);
        m.value.push_back(ctx.sec_on ? 1 : 0);
        m.value.insert(m.value.end(), ctx.up_key.begin(), ctx.up_key.end());
        m.value.push_back(static_cast<uint8_t>(ctx.imsi.size()));
        m.value.insert(m.value.end(), ctx.imsi.begin(), ctx.imsi.end());
        cn_amf_->send(m);
        initiated_ho_[crnti] = {target_cell_id, 0}; // crnti unknown until HO_COMMAND
        return;
    }

    LOG_INFO(ev::HANDOVER_START,
             {{"imsi", ctx.imsi},
              {"from", std::to_string(config_.cell_id)},
              {"to", std::to_string(target_cell_id)}});
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
                           const std::vector<uint8_t>& sdu) {
    auto& f = flow(rnti);
    // M17: route into the QoS bearer queue (app LCIDs) or the control queue
    // (CCCH/NAS/RLC STATUS). Ciphering happens at drain time in
    // schedule_downlink so the shared PDCP COUNT matches air order.
    std::deque<AppPdu>* q;
    if (auto qci = qci_of_lcid(lcid)) {
        q = &f.bearers.queue_of(*qci);
    } else {
        q = &f.bearers.ctrl();
    }
    if (q->size() >= config_.max_dl_queue_per_ue) {
        q->pop_front(); // backpressure: shed the oldest
        ++f.dropped;
    }
    // The security decision is captured at enqueue time (the ATTACH_ACCEPT
    // goes out in the clear even though security arms right after it).
    const bool cipher = f.sec_on && (lcid == mac::LCID_NAS_DCCH ||
                                     qci_of_lcid(lcid).has_value());
    q->push_back({lcid, cipher, sdu});
    ++f.enqueued;
}

bool BsNode::flow_bearer_established(uint16_t rnti, int qci) const {
    auto it = flows_.find(rnti);
    return it != flows_.end() &&
           it->second.bearers.established_of(static_cast<Qci>(qci));
}

// M17: QOS_BEARER_SETUP on first use of a dedicated bearer on this flow.
void BsNode::ensure_bs_bearer(uint16_t rnti, DlFlow& f, Qci qci) {    if (qci == Qci::BEST_EFFORT) return; // default bearer always exists
    auto& est = f.bearers.established_of(qci);
    if (est) return;
    est = true;
    LOG_INFO(ev::QOS_BEARER_SETUP,
             {{"c_rnti", std::to_string(rnti)},
              {"qci", std::to_string(static_cast<int>(qci))},
              {"kind", bearer_kind_name(qci)}});
}

// M17: QOS_BEARER_TEARDOWN for every established dedicated bearer — the BS
// keeps bearers for the flow's lifetime, so this fires when the flow goes
// away (detach / handover release). See docs/m17_plan.md.
void BsNode::log_bs_bearer_teardowns(uint16_t rnti, DlFlow& f) {
    for (Qci qci : {Qci::SIG, Qci::VOICE, Qci::VIDEO}) {
        auto& est = f.bearers.established_of(qci);
        if (!est) continue;
        est = false;
        LOG_INFO(ev::QOS_BEARER_TEARDOWN,
                 {{"c_rnti", std::to_string(rnti)},
                  {"qci", std::to_string(static_cast<int>(qci))},
                  {"kind", bearer_kind_name(qci)}});
    }
}

void BsNode::downlink_raw(uint16_t rnti, uint8_t lcid,
                          const std::vector<uint8_t>& sdu) {
    std::vector<uint8_t> pdu = mac::build_pdu({{lcid, sdu}});
    send_frame(AirFrameType::DATA, rnti, pdu);
}

void BsNode::send_frame(AirFrameType type, uint16_t rnti,
                        const std::vector<uint8_t>& payload) {
    AirFrame frame;
    frame.type = type;
    frame.rnti = rnti;
    frame.payload = payload;
    auto bits = pack_air_bits(encode_frame(frame));
    if (air_send_ex_) { // M19: MCS-carrying radio path (process shell)
        air_send_ex_(frame, mcs_for_frame(frame), bits);
        return;
    }
    if (!air_send_) return;
    air_send_(bits);
}

}

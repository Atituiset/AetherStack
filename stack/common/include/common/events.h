#ifndef AETHER_COMMON_EVENTS_H
#define AETHER_COMMON_EVENTS_H

// M6.5 D5: single source of truth for structured log event names.
//
// Contract:
//   * Every event emitted by the stack MUST be listed here and referenced
//     as ev::<NAME> at the call site (CI greps for bare string literals).
//   * The Web LMT mirrors this file in lmt/src/events.ts.
//   * Documented fields are part of the public contract for the frontend.
//
// Field conventions: values are strings; numbers are decimal unless the
// field name ends with _hex. layer/direction/hex/brief belong to PDU_TRACE.

namespace ev {

// ---- Process lifecycle ----------------------------------------------------
inline constexpr char PROCESS_START[] = "PROCESS_START";     // {msg}
inline constexpr char PROCESS_EXIT[] = "PROCESS_EXIT";       // {}
inline constexpr char HEARTBEAT[] = "HEARTBEAT";             // period 5s; module-specific counters
inline constexpr char UE_CMD_HINT[] = "UE_CMD_HINT";         // {cmd}
inline constexpr char UE_STATUS[] = "UE_STATUS";             // {mac,rrc,nas,c_rnti,sib,app_rx}
inline constexpr char BS_STATUS[] = "BS_STATUS";             // {registered_ues}
inline constexpr char UE_DETACH_IGNORED[] = "UE_DETACH_IGNORED"; // UE {state}
inline constexpr char BS_SIB_BROADCAST_ON[] = "BS_SIB_BROADCAST_ON"; // BS {period_ms}

// ---- PHY / transport ------------------------------------------------------
inline constexpr char PHY_CONFIG[] = "PHY_CONFIG";           // {n_fft, cp_len}
inline constexpr char PHY_BIND_FAIL[] = "PHY_BIND_FAIL";     // {port}
inline constexpr char PHY_UDP_READY[] = "PHY_UDP_READY";     // {local_port, dest}

// ---- Core / air framing ---------------------------------------------------
inline constexpr char AIR_FRAME_DECODE_FAIL[] = "AIR_FRAME_DECODE_FAIL"; // {len}
inline constexpr char SYSINFO_DECODE_FAIL[] = "SYSINFO_DECODE_FAIL";     // {lcid}

// ---- MAC RACH (4-step) ----------------------------------------------------
// State machine: IDLE -> WAIT_RAR -> WAIT_CONTENTION_RESOLVE -> CONNECTED
inline constexpr char MAC_STATE_CHANGE[] = "MAC_STATE_CHANGE"; // {layer, old_state, new_state}
inline constexpr char MAC_RACH_MSG1[] = "MAC_RACH_MSG1";       // UE->BS {preamble, tx_count}
inline constexpr char MAC_RACH_MSG2[] = "MAC_RACH_MSG2";       // BS->UE {ra_rnti, preamble, ta}
inline constexpr char MAC_RACH_MSG2_RX[] = "MAC_RACH_MSG2_RX"; // UE {ra_rnti, ta}
inline constexpr char MAC_RACH_MSG3[] = "MAC_RACH_MSG3";       // UE->BS {ra_rnti, ccch_len}
inline constexpr char MAC_RACH_MSG4[] = "MAC_RACH_MSG4";       // BS->UE {ra_rnti, c_rnti}
inline constexpr char MAC_RACH_MSG4_RX[] = "MAC_RACH_MSG4_RX"; // UE {c_rnti}
inline constexpr char RACH_SUCCESS[] = "RACH_SUCCESS";         // UE {c_rnti}
// M22: preamble partitioning — this MSG1 targets another cell (debug).
inline constexpr char RACH_PREAMBLE_FOREIGN_CELL[] = "RACH_PREAMBLE_FOREIGN_CELL"; // BS {preamble, cell}
// M22: a setup/resume request addressed to another cell, ignored (debug).
inline constexpr char RRC_SETUP_FOREIGN_CELL[] = "RRC_SETUP_FOREIGN_CELL"; // BS {target|resume_id, cell}
inline constexpr char RA_SUCCESS[] = "RA_SUCCESS";             // BS {c_rnti}
inline constexpr char RACH_BACKOFF[] = "RACH_BACKOFF";         // UE {delay_ms}
inline constexpr char RACH_RAR_TIMEOUT[] = "RACH_RAR_TIMEOUT"; // UE {retry}
inline constexpr char RACH_CR_TIMEOUT[] = "RACH_CR_TIMEOUT";   // UE {}
inline constexpr char RACH_FAILED[] = "RACH_FAILED";           // UE {reason}
inline constexpr char RACH_START_IGNORED[] = "RACH_START_IGNORED"; // UE {state}
inline constexpr char RAR_IGNORED[] = "RAR_IGNORED";           // UE {state}
inline constexpr char CR_IGNORED[] = "CR_IGNORED";             // UE {state}
inline constexpr char MSG3_UNKNOWN_RA_RNTI[] = "MSG3_UNKNOWN_RA_RNTI"; // BS {ra_rnti}
inline constexpr char UE_RACH_TX_UNEXPECTED[] = "UE_RACH_TX_UNEXPECTED"; // UE {type}
inline constexpr char BS_RACH_TX_UNEXPECTED[] = "BS_RACH_TX_UNEXPECTED"; // BS {type}

// ---- RRC ------------------------------------------------------------------
inline constexpr char RRC_UE_STATE[] = "RRC_UE_STATE";         // UE {old, new}: IDLE|CONNECTING|CONNECTED
inline constexpr char RRC_MIB_RX[] = "RRC_MIB_RX";             // UE {sfn, bw}
inline constexpr char RRC_SIB1_RX[] = "RRC_SIB1_RX";           // UE {plmn, tac, cell_id}
inline constexpr char RRC_SETUP_REQUEST_TX[] = "RRC_SETUP_REQUEST_TX"; // UE {}
inline constexpr char RRC_SETUP_TX[] = "RRC_SETUP_TX";         // BS->UE {c_rnti}
inline constexpr char RRC_SETUP_RX[] = "RRC_SETUP_RX";         // UE {c_rnti}
inline constexpr char RRC_SETUP_COMPLETE_TX[] = "RRC_SETUP_COMPLETE_TX"; // UE {c_rnti}
inline constexpr char RRC_SETUP_COMPLETE_RX[] = "RRC_SETUP_COMPLETE_RX"; // BS {c_rnti}
inline constexpr char RRC_UE_CONNECTED[] = "RRC_UE_CONNECTED"; // BS {c_rnti}
inline constexpr char RRC_RELEASE_TX[] = "RRC_RELEASE_TX";     // UE {c_rnti}
inline constexpr char RRC_RELEASED[] = "RRC_RELEASED";         // UE {}
inline constexpr char RRC_UE_RELEASED[] = "RRC_UE_RELEASED";   // BS {c_rnti}
inline constexpr char RRC_SETUP_IGNORED[] = "RRC_SETUP_IGNORED";       // UE {state}
inline constexpr char RRC_SETUP_RX_IGNORED[] = "RRC_SETUP_RX_IGNORED"; // UE {state}
inline constexpr char RRC_RELEASE_IGNORED[] = "RRC_RELEASE_IGNORED";   // UE {state}
inline constexpr char RRC_SETUP_COMPLETE_UNKNOWN[] = "RRC_SETUP_COMPLETE_UNKNOWN"; // BS {c_rnti}

// ---- NAS ------------------------------------------------------------------
inline constexpr char NAS_STATE_CHANGE[] = "NAS_STATE_CHANGE"; // {old, new}: DEREGISTERED|REGISTERING|REGISTERED
inline constexpr char NAS_ATTACH_REQUEST_TX[] = "NAS_ATTACH_REQUEST_TX"; // UE {imsi}
inline constexpr char NAS_ATTACH_ACCEPT_TX[] = "NAS_ATTACH_ACCEPT_TX";   // BS {imsi, tmsi}
inline constexpr char NAS_ATTACH_ACCEPT_RX[] = "NAS_ATTACH_ACCEPT_RX";   // UE {tmsi}
inline constexpr char NAS_ATTACH_REJECT_RX[] = "NAS_ATTACH_REJECT_RX";   // UE {}
inline constexpr char NAS_DETACH_TX[] = "NAS_DETACH_TX";       // UE {tmsi}
inline constexpr char NAS_DETACH_RX[] = "NAS_DETACH_RX";       // BS {imsi, tmsi}
inline constexpr char NAS_DETACH_UNKNOWN[] = "NAS_DETACH_UNKNOWN"; // BS {tmsi}
inline constexpr char NAS_ACCEPT_IGNORED[] = "NAS_ACCEPT_IGNORED"; // UE {state}
inline constexpr char NAS_ATTACH_REQ_IGNORED[] = "NAS_ATTACH_REQ_IGNORED"; // UE {state}
inline constexpr char NAS_DETACH_IGNORED[] = "NAS_DETACH_IGNORED"; // UE {state}

// ---- Security (M12 primitives; M21 5G-AKA-style exchange) --------------------
inline constexpr char SEC_ENABLED[] = "SEC_ENABLED";     // {dir}
inline constexpr char SEC_DECRYPT_FAIL[] = "SEC_DECRYPT_FAIL"; // {layer}
// M21 AKA: BS/UDM sends [RAND||AUTN] (rand and SQN^AK are hex-masked to the
// first bytes), UE answers RES, BS confirms. cause is one of
// mac | synch | res_mismatch.
inline constexpr char NAS_AUTH_VECTOR[] = "NAS_AUTH_VECTOR";   // BS {imsi, rand, sqn_masked}
inline constexpr char NAS_AUTH_RES[] = "NAS_AUTH_RES";         // UE {imsi, res}
inline constexpr char NAS_AUTH_SUCCESS[] = "NAS_AUTH_SUCCESS"; // BS {imsi}
inline constexpr char NAS_AUTH_FAIL[] = "NAS_AUTH_FAIL";       // UE/BS {imsi, cause}
inline constexpr char NAS_AUTH_RESP_UNKNOWN[] = "NAS_AUTH_RESP_UNKNOWN";
inline constexpr char NAS_AUTH_REQ_IGNORED[] = "NAS_AUTH_REQ_IGNORED"; // {state}

// ---- Link reliability (M9 HARQ/FEC) ----------------------------------------
inline constexpr char HARQ_RETX[] = "HARQ_RETX";   // {proc, attempt, reason(nack|timeout)}
inline constexpr char HARQ_DROP[] = "HARQ_DROP";   // {proc, attempts}: budget exhausted
inline constexpr char HARQ_COMBINE[] = "HARQ_COMBINE"; // RX {proc}: chase merging

// ---- RLC UM/AM (M13) --------------------------------------------------------
inline constexpr char RLC_UM_GAP_SKIP[] = "RLC_UM_GAP_SKIP"; // {skipped}: reorder timeout (UM + AM)
inline constexpr char RLC_AM_STATUS_TX[] = "RLC_AM_STATUS_TX"; // {dir, nacks}
inline constexpr char RLC_AM_RETX[] = "RLC_AM_RETX";           // {sn}
inline constexpr char PDCP_MAC_FAIL[] = "PDCP_MAC_FAIL";       // {}: integrity check

// ---- Mobility (M14) ---------------------------------------------------------
// M22 (frontend contract): dual-BS handover lifecycle. from/to are cell ids
// as decimal strings; path is "ho" (Xn/coordinated) — "reest" is reserved
// for the re-establishment fallback.
inline constexpr char HANDOVER_START[] = "HANDOVER_START"; // BS {imsi, from, to}
inline constexpr char HANDOVER_DONE[] = "HANDOVER_DONE";   // BS {imsi, from, to, path}
inline constexpr char MEAS_REPORT_TX[] = "MEAS_REPORT_TX";     // {serving, n}
inline constexpr char HO_TRIGGERED[] = "HO_TRIGGERED";         // {from_cell, to_cell}
inline constexpr char HO_COMMAND_TX[] = "HO_COMMAND_TX";       // {cell, rnti}
inline constexpr char HO_COMPLETE_RX[] = "HO_COMPLETE_RX";     // {cell, rnti}
inline constexpr char PAGE_TX[] = "PAGE_TX";                   // {imsi}
inline constexpr char PAGE_RX[] = "PAGE_RX";                   // {imsi}
inline constexpr char RLF_DETECTED[] = "RLF_DETECTED";         // {crnti}
inline constexpr char RRC_REEST_REQ_TX[] = "RRC_REEST_REQ_TX"; // {old_crnti}
inline constexpr char RRC_REEST_OK[] = "RRC_REEST_OK";         // {old, new}
inline constexpr char RRC_REEST_FAIL[] = "RRC_REEST_FAIL";     // {c_rnti}
inline constexpr char NG_SETUP_RX[] = "NG_SETUP_RX";           // AMF {cell}
inline constexpr char UPF_PATH_SWITCH[] = "UPF_PATH_SWITCH";   // UPF {tmsi, cell}
inline constexpr char UPF_NO_ROUTE[] = "UPF_NO_ROUTE";         // UPF {tmsi}

// ---- Attach orchestration -------------------------------------------------
inline constexpr char UE_ATTACH_PENDING_SIB[] = "UE_ATTACH_PENDING_SIB"; // UE {}
inline constexpr char UE_ATTACH_START[] = "UE_ATTACH_START"; // UE {imsi}
inline constexpr char ATTACH_RETRY[] = "ATTACH_RETRY";       // UE {delay_ms}: RACH collapsed, re-running
inline constexpr char ATTACH_ABORT[] = "ATTACH_ABORT";       // UE {reason}
inline constexpr char UE_DETACH_DONE[] = "UE_DETACH_DONE";   // UE {}

// ---- User plane -----------------------------------------------------------
inline constexpr char APP_DATA_TX[] = "APP_DATA_TX";          // app layer {len}
inline constexpr char APP_DATA_RX[] = "APP_DATA_RX";          // app layer {len}
inline constexpr char APP_ECHO_TX[] = "APP_ECHO_TX";          // BS {len}
inline constexpr char APP_RTT[] = "APP_RTT";                  // UE {seq, rtt_ms}
inline constexpr char APP_LOSS[] = "APP_LOSS";                // UE {seq}: unanswered past 3s window
inline constexpr char APP_TX_NO_CONTEXT[] = "APP_TX_NO_CONTEXT"; // UE {}

// ---- Traffic loopback (M7.1) ------------------------------------------------
inline constexpr char TRAFFIC_START[] = "TRAFFIC_START";      // UE {interval_ms}
inline constexpr char TRAFFIC_STOP[] = "TRAFFIC_STOP";        // UE {}
// Periodic aggregate: {tx, rx, loss, rtt_min, rtt_max, rtt_avg}
inline constexpr char TRAFFIC_STATS[] = "TRAFFIC_STATS";

// ---- UE-to-UE media (M16) -----------------------------------------------------
// kind is one of "voice" | "video" | "msg"; src/dst are IMSI strings.
// M17 (SIP-lite) remaps the call lifecycle onto a real dialog:
//   APP_CALL_START    fires when the caller sends the INVITE (was: media start)
//   APP_CALL_INCOMING fires when the INVITE arrives / callee rings
//                     (was: first media packet)
//   APP_CALL_END      local hangup: BYE when established, CANCEL while ringing
//   APP_CALL_PEER_END BYE (or CANCEL) received from the peer
// Media (and thus APP_STREAM_STATS) only flows once the dialog is
// established (200 OK + ACK).
inline constexpr char APP_MSG_TX[] = "APP_MSG_TX";          // UE {dst, text}
inline constexpr char APP_MSG_RX[] = "APP_MSG_RX";          // UE {src, text}
inline constexpr char APP_CALL_START[] = "APP_CALL_START";  // UE {dst, kind}
inline constexpr char APP_CALL_END[] = "APP_CALL_END";      // UE {dst, kind}
inline constexpr char APP_CALL_INCOMING[] = "APP_CALL_INCOMING"; // UE {src, kind}
inline constexpr char APP_CALL_PEER_END[] = "APP_CALL_PEER_END"; // UE {src, kind}
// BS forward: msg and sig log immediately (count=1); voice/video aggregate
// over a 1 s window -> {src, dst, kind, bytes, count}.
inline constexpr char APP_FORWARD[] = "APP_FORWARD";        // BS {src, dst, kind, bytes, count}
// Both UEs during an active stream, every ~5 s. TX side: tx=media, rx=acks
// received (plus deliveries proven by later acks), rtt_avg from acks;
// RX side: tx=acks sent, rx=media, loss=seq gaps, rtt_avg=-1.
// M17: qci identifies the dedicated bearer ("1" voice, "2" video).
// M18: conference streams use kind "conf" (voice QCI) and add conf_id.
inline constexpr char APP_STREAM_STATS[] = "APP_STREAM_STATS"; // UE {kind, peer, tx, rx, loss, rtt_avg, qci[, conf_id]}

// ---- SIP-lite call control (M17) ---------------------------------------------
// Dialog: idle -> INVITE -> calling/ringing -> 200 OK + ACK -> established
// (media) -> BYE -> idle. reason is one of
// busy | declined | unreachable | timeout | cancel.
inline constexpr char SIP_INVITE_TX[] = "SIP_INVITE_TX";   // UE caller {dst, kind}
inline constexpr char SIP_INVITE_RX[] = "SIP_INVITE_RX";   // UE callee {src, kind}
inline constexpr char SIP_RINGING_TX[] = "SIP_RINGING_TX"; // UE callee {dst}: 180 sent
inline constexpr char SIP_RINGING_RX[] = "SIP_RINGING_RX"; // UE caller {src}: 180 received
inline constexpr char SIP_CALL_ESTABLISHED[] = "SIP_CALL_ESTABLISHED"; // UE {peer, kind} both sides
inline constexpr char SIP_CALL_FAILED[] = "SIP_CALL_FAILED"; // UE {peer, reason}
inline constexpr char SIP_BYE_TX[] = "SIP_BYE_TX";         // UE {peer}
inline constexpr char SIP_BYE_RX[] = "SIP_BYE_RX";         // UE {peer}

// ---- QoS dedicated bearers (M17) ----------------------------------------------
// kind is "sig" | "voice" | "video" (the default QCI9 bearer always exists
// and logs no setup). qci is the QCI number as a decimal string.
inline constexpr char QOS_BEARER_SETUP[] = "QOS_BEARER_SETUP";     // UE/BS {c_rnti, qci, kind}
inline constexpr char QOS_BEARER_TEARDOWN[] = "QOS_BEARER_TEARDOWN"; // UE/BS {c_rnti, qci, kind}

// ---- M18: 3-party conference (audio bridge at the BS) -------------------------
// The BS snoops forwarded SIP-lite signaling: a conf_id-bearing INVITE opens
// the conference (CONF_START), each 200 OK joins a party (CONF_JOIN),
// BYE/486/603/CANCEL/detach removes one (CONF_LEAVE, reason =
// hangup|busy|decline|cancel|detach) and the conference closes when the
// host leaves ("host"), the membership drops below two ("empty") or every
// invitation was refused ("no-parties").
inline constexpr char CONF_START[] = "CONF_START"; // BS {host, conf_id}
inline constexpr char CONF_JOIN[] = "CONF_JOIN";   // BS {conf_id, imsi}
inline constexpr char CONF_LEAVE[] = "CONF_LEAVE"; // BS {conf_id, imsi, reason}
inline constexpr char CONF_END[] = "CONF_END";     // BS {conf_id, reason}

// ---- M19: link adaptation, TX power control, signal quality -------------------
// CQI (1-15) is derived at the UE from the DMRS SNR of decoded DL bursts and
// reported via a MAC CE; the BS maps it onto the MCS ladder
// (qpsk < cqi 14 | 16qam >= 14, from the measured decode curve; 64QAM is
// implemented but never selected) per downlink flow.
// CQI_REPORT logs change-only; MCS_CHANGE on every re-selection.
inline constexpr char CQI_REPORT[] = "CQI_REPORT";   // BS {c_rnti, cqi, snr_db}
inline constexpr char MCS_CHANGE[] = "MCS_CHANGE";   // BS {c_rnti, mcs, direction}
// TX power: open-loop from measured DL pathloss plus closed-loop TPC
// (+/-1 dB) from the BS. TX_POWER_CHANGE logs at >=0.5 dB deltas, >=1 s apart.
inline constexpr char TX_POWER_CHANGE[] = "TX_POWER_CHANGE"; // UE {c_rnti, dbm}
// RSRP/SINR-like link metrics for the LMT signal bars, ~1/s while registered.
inline constexpr char LINK_QUALITY[] = "LINK_QUALITY"; // UE {c_rnti, rsrp, sinr}

// ---- M20: RRC_INACTIVE + fast resume -------------------------------------------
// After an inactivity timeout (or a UE suspend request) the connected UE is
// suspended to RRC_INACTIVE: context (C-RNTI binding, keys, bearers) and NAS
// registration are kept on both sides. The next activity resumes via RACH +
// RRCResumeRequest{resume_id} — no RRC setup, no NAS re-attach. A stale
// resume identity falls back to full setup.
inline constexpr char RRC_INACTIVE[] = "RRC_INACTIVE";   // UE/BS {c_rnti, resume_id}
inline constexpr char RRC_RESUME_REQUEST[] = "RRC_RESUME_REQUEST"; // UE {resume_id} / BS {resume_id, c_rnti}
inline constexpr char RRC_RESUMED[] = "RRC_RESUMED";     // UE/BS {c_rnti, old_c_rnti}
inline constexpr char RRC_RESUME_FAIL[] = "RRC_RESUME_FAIL"; // BS {resume_id, reason} / UE {reason}

// ---- PDU inspector --------------------------------------------------------
// fields: {layer, direction(TX|RX), len, hex(capped 48B), brief}
inline constexpr char PDU_TRACE[] = "PDU_TRACE";

// ---- Demo orchestration (M8.2) ---------------------------------------------
// Emitted by tools/demo/demo_scenario.py (Python side) into the log pipeline,
// not by the C++ stack; rendered as a progress banner in the Web LMT.
// fields: {phase(boot|attach|traffic|release|done), title, progress, detail}
inline constexpr char DEMO_PHASE[] = "DEMO_PHASE";

} // namespace ev

#endif // AETHER_COMMON_EVENTS_H

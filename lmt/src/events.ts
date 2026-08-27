// Mirror of stack/common/include/common/events.h (M6.5 D5).
// Keep in sync: CI greps both files for event-name drift.
// Field contracts are documented on each constant.

export const ev = {
  // Process lifecycle
  PROCESS_START: 'PROCESS_START',
  PROCESS_EXIT: 'PROCESS_EXIT',
  HEARTBEAT: 'HEARTBEAT', // {c_rnti, registered} | {registered_ues}
  UE_CMD_HINT: 'UE_CMD_HINT',
  UE_STATUS: 'UE_STATUS', // {mac, rrc, nas, c_rnti, sib, app_rx}
  BS_STATUS: 'BS_STATUS', // {registered_ues}
  UE_DETACH_IGNORED: 'UE_DETACH_IGNORED',

  // PHY / transport
  PHY_CONFIG: 'PHY_CONFIG', // {n_fft, cp_len}
  PHY_BIND_FAIL: 'PHY_BIND_FAIL',
  PHY_UDP_READY: 'PHY_UDP_READY',

  // Core / air framing
  AIR_FRAME_DECODE_FAIL: 'AIR_FRAME_DECODE_FAIL',
  SYSINFO_DECODE_FAIL: 'SYSINFO_DECODE_FAIL',
  BS_SIB_BROADCAST_ON: 'BS_SIB_BROADCAST_ON',

  // MAC RACH
  MAC_STATE_CHANGE: 'MAC_STATE_CHANGE', // {layer, old_state, new_state}
  MAC_RACH_MSG1: 'MAC_RACH_MSG1',
  MAC_RACH_MSG2: 'MAC_RACH_MSG2',
  MAC_RACH_MSG2_RX: 'MAC_RACH_MSG2_RX',
  MAC_RACH_MSG3: 'MAC_RACH_MSG3',
  MAC_RACH_MSG4: 'MAC_RACH_MSG4',
  MAC_RACH_MSG4_RX: 'MAC_RACH_MSG4_RX',
  RACH_SUCCESS: 'RACH_SUCCESS',
  RA_SUCCESS: 'RA_SUCCESS',
  RACH_BACKOFF: 'RACH_BACKOFF',
  RACH_RAR_TIMEOUT: 'RACH_RAR_TIMEOUT',
  RACH_CR_TIMEOUT: 'RACH_CR_TIMEOUT',
  RACH_FAILED: 'RACH_FAILED',
  RACH_START_IGNORED: 'RACH_START_IGNORED',
  RAR_IGNORED: 'RAR_IGNORED',
  CR_IGNORED: 'CR_IGNORED',
  MSG3_UNKNOWN_RA_RNTI: 'MSG3_UNKNOWN_RA_RNTI',
  UE_RACH_TX_UNEXPECTED: 'UE_RACH_TX_UNEXPECTED',
  BS_RACH_TX_UNEXPECTED: 'BS_RACH_TX_UNEXPECTED',

  // RRC
  RRC_UE_STATE: 'RRC_UE_STATE', // {old, new}: IDLE | CONNECTING | CONNECTED
  RRC_MIB_RX: 'RRC_MIB_RX', // {sfn, bw}
  RRC_SIB1_RX: 'RRC_SIB1_RX', // {plmn, tac, cell_id}
  RRC_SETUP_REQUEST_TX: 'RRC_SETUP_REQUEST_TX',
  RRC_SETUP_TX: 'RRC_SETUP_TX',
  RRC_SETUP_RX: 'RRC_SETUP_RX',
  RRC_SETUP_COMPLETE_TX: 'RRC_SETUP_COMPLETE_TX',
  RRC_SETUP_COMPLETE_RX: 'RRC_SETUP_COMPLETE_RX',
  RRC_UE_CONNECTED: 'RRC_UE_CONNECTED',
  RRC_RELEASE_TX: 'RRC_RELEASE_TX',
  RRC_RELEASED: 'RRC_RELEASED',
  RRC_UE_RELEASED: 'RRC_UE_RELEASED',
  RRC_SETUP_IGNORED: 'RRC_SETUP_IGNORED',
  RRC_SETUP_RX_IGNORED: 'RRC_SETUP_RX_IGNORED',
  RRC_RELEASE_IGNORED: 'RRC_RELEASE_IGNORED',
  RRC_SETUP_COMPLETE_UNKNOWN: 'RRC_SETUP_COMPLETE_UNKNOWN',

  // NAS
  NAS_STATE_CHANGE: 'NAS_STATE_CHANGE', // {old, new}: DEREGISTERED | REGISTERING | REGISTERED
  NAS_ATTACH_REQUEST_TX: 'NAS_ATTACH_REQUEST_TX',
  NAS_ATTACH_ACCEPT_TX: 'NAS_ATTACH_ACCEPT_TX',
  NAS_ATTACH_ACCEPT_RX: 'NAS_ATTACH_ACCEPT_RX',
  NAS_ATTACH_REJECT_RX: 'NAS_ATTACH_REJECT_RX',
  NAS_DETACH_TX: 'NAS_DETACH_TX',
  NAS_DETACH_RX: 'NAS_DETACH_RX',
  NAS_DETACH_UNKNOWN: 'NAS_DETACH_UNKNOWN',
  NAS_ACCEPT_IGNORED: 'NAS_ACCEPT_IGNORED',
  NAS_ATTACH_REQ_IGNORED: 'NAS_ATTACH_REQ_IGNORED',
  NAS_DETACH_IGNORED: 'NAS_DETACH_IGNORED',

  // Security (M12)
  NAS_AUTH_VECTOR: 'NAS_AUTH_VECTOR', // BS {imsi, rand, sqn_masked} — M21 AKA
  NAS_AUTH_RES: 'NAS_AUTH_RES', // UE {imsi, res}
  NAS_AUTH_SUCCESS: 'NAS_AUTH_SUCCESS', // BS {imsi}
  NAS_AUTH_FAIL: 'NAS_AUTH_FAIL', // UE/BS {imsi, cause}: mac|synch|res_mismatch
  NAS_AUTH_RESP_UNKNOWN: 'NAS_AUTH_RESP_UNKNOWN',
  NAS_AUTH_REQ_IGNORED: 'NAS_AUTH_REQ_IGNORED',
  SEC_ENABLED: 'SEC_ENABLED',
  SEC_DECRYPT_FAIL: 'SEC_DECRYPT_FAIL',

  // Link reliability (M9)
  HARQ_RETX: 'HARQ_RETX',
  HARQ_DROP: 'HARQ_DROP',

  // RLC UM/AM + PDCP integrity (M13)
  RLC_UM_GAP_SKIP: 'RLC_UM_GAP_SKIP',
  RLC_AM_STATUS_TX: 'RLC_AM_STATUS_TX',
  RLC_AM_RETX: 'RLC_AM_RETX',
  PDCP_MAC_FAIL: 'PDCP_MAC_FAIL',

  // Mobility (M14)
  MEAS_REPORT_TX: 'MEAS_REPORT_TX',
  HO_TRIGGERED: 'HO_TRIGGERED',
  HO_COMMAND_TX: 'HO_COMMAND_TX',
  HO_COMPLETE_RX: 'HO_COMPLETE_RX',
  PAGE_TX: 'PAGE_TX',
  PAGE_RX: 'PAGE_RX',
  RLF_DETECTED: 'RLF_DETECTED',
  RRC_REEST_REQ_TX: 'RRC_REEST_REQ_TX',
  RRC_REEST_OK: 'RRC_REEST_OK',
  RRC_REEST_FAIL: 'RRC_REEST_FAIL',
  NG_SETUP_RX: 'NG_SETUP_RX',
  UPF_PATH_SWITCH: 'UPF_PATH_SWITCH',
  UPF_NO_ROUTE: 'UPF_NO_ROUTE',
  HARQ_COMBINE: 'HARQ_COMBINE',

  // Attach orchestration
  UE_ATTACH_PENDING_SIB: 'UE_ATTACH_PENDING_SIB',
  UE_ATTACH_START: 'UE_ATTACH_START',
  ATTACH_RETRY: 'ATTACH_RETRY',
  ATTACH_ABORT: 'ATTACH_ABORT',
  UE_DETACH_DONE: 'UE_DETACH_DONE',

  // User plane
  APP_DATA_TX: 'APP_DATA_TX',
  APP_DATA_RX: 'APP_DATA_RX',
  APP_ECHO_TX: 'APP_ECHO_TX',
  APP_RTT: 'APP_RTT', // {seq, rtt_ms}
  APP_LOSS: 'APP_LOSS', // {seq}
  APP_TX_NO_CONTEXT: 'APP_TX_NO_CONTEXT',

  // Traffic loopback (M7.1)
  TRAFFIC_START: 'TRAFFIC_START', // {interval_ms}
  TRAFFIC_STOP: 'TRAFFIC_STOP',
  TRAFFIC_STATS: 'TRAFFIC_STATS', // {tx, rx, loss, rtt_min, rtt_max, rtt_avg}

  // UE-to-UE media (M16): kind in "voice" | "video" | "msg"; src/dst are IMSIs
  APP_MSG_TX: 'APP_MSG_TX', // UE {dst, text}
  APP_MSG_RX: 'APP_MSG_RX', // UE {src, text}
  APP_CALL_START: 'APP_CALL_START', // UE {dst, kind} — M17: at INVITE
  APP_CALL_END: 'APP_CALL_END', // UE {dst, kind} — M17: BYE/CANCEL sent
  APP_CALL_INCOMING: 'APP_CALL_INCOMING', // UE {src, kind} — M17: at INVITE/ringing
  APP_CALL_PEER_END: 'APP_CALL_PEER_END', // UE {src, kind} — M17: BYE/CANCEL received
  APP_FORWARD: 'APP_FORWARD', // BS {src, dst, kind, bytes, count}
  APP_STREAM_STATS: 'APP_STREAM_STATS', // UE {kind, peer, tx, rx, loss, rtt_avg, qci[, conf_id]} — M18: kind "conf" for conference streams

  // SIP-lite call control (M17): dialog idle->INVITE->ringing->200+ACK->established->BYE
  SIP_INVITE_TX: 'SIP_INVITE_TX', // UE caller {dst, kind}
  SIP_INVITE_RX: 'SIP_INVITE_RX', // UE callee {src, kind}
  SIP_RINGING_TX: 'SIP_RINGING_TX', // UE callee {dst}
  SIP_RINGING_RX: 'SIP_RINGING_RX', // UE caller {src}
  SIP_CALL_ESTABLISHED: 'SIP_CALL_ESTABLISHED', // UE {peer, kind}
  SIP_CALL_FAILED: 'SIP_CALL_FAILED', // UE {peer, reason}: busy|declined|unreachable|timeout|cancel
  SIP_BYE_TX: 'SIP_BYE_TX', // UE {peer}
  SIP_BYE_RX: 'SIP_BYE_RX', // UE {peer}

  // QoS dedicated bearers (M17): kind in "sig" | "voice" | "video"
  QOS_BEARER_SETUP: 'QOS_BEARER_SETUP', // UE/BS {c_rnti, qci, kind}
  QOS_BEARER_TEARDOWN: 'QOS_BEARER_TEARDOWN', // UE/BS {c_rnti, qci, kind}

  // 3-party conference, audio bridge at the BS (M18)
  CONF_START: 'CONF_START', // BS {host, conf_id}
  CONF_JOIN: 'CONF_JOIN', // BS {conf_id, imsi}
  CONF_LEAVE: 'CONF_LEAVE', // BS {conf_id, imsi, reason}: hangup|busy|decline|cancel|detach
  CONF_END: 'CONF_END', // BS {conf_id, reason}: host|empty|no-parties

  // M19: link adaptation / TX power control / signal quality
  CQI_REPORT: 'CQI_REPORT', // BS {c_rnti, cqi, snr_db} — change-only
  MCS_CHANGE: 'MCS_CHANGE', // BS {c_rnti, mcs, direction}
  TX_POWER_CHANGE: 'TX_POWER_CHANGE', // UE {c_rnti, dbm} — >=0.5 dB, >=1 s
  LINK_QUALITY: 'LINK_QUALITY', // UE {c_rnti, rsrp, sinr} — ~1/s

  RACH_PREAMBLE_FOREIGN_CELL: 'RACH_PREAMBLE_FOREIGN_CELL', // BS {preamble, cell} (debug)
  RRC_SETUP_FOREIGN_CELL: 'RRC_SETUP_FOREIGN_CELL', // BS {target|resume_id, cell} (debug)

  // M22: dual-BS mobility handover
  HANDOVER_START: 'HANDOVER_START', // BS {imsi, from, to}
  HANDOVER_DONE: 'HANDOVER_DONE', // BS {imsi, from, to, path}: path=ho|reest

  // M20: RRC_INACTIVE + fast resume
  RRC_INACTIVE: 'RRC_INACTIVE', // UE/BS {c_rnti, resume_id}
  RRC_RESUME_REQUEST: 'RRC_RESUME_REQUEST', // UE {resume_id} / BS {resume_id, c_rnti}
  RRC_RESUMED: 'RRC_RESUMED', // UE/BS {c_rnti, old_c_rnti}
  RRC_RESUME_FAIL: 'RRC_RESUME_FAIL', // BS {resume_id, reason} / UE {reason}

  // PDU inspector: fields {layer, direction, len, hex, brief}
  PDU_TRACE: 'PDU_TRACE',

  // Demo orchestrator (module=DEMO): fields {phase,title,progress,detail}
  DEMO_PHASE: 'DEMO_PHASE',
} as const

export type EventName = (typeof ev)[keyof typeof ev]

/** Events rendered as UE->BS arrows in the MSC diagram. */
export const MSC_UPLINK: Partial<Record<EventName, string>> = {
  MAC_RACH_MSG1: 'MSG1: PRACH',
  MAC_RACH_MSG3: 'MSG3: RRC Req',
  RRC_SETUP_REQUEST_TX: 'RRC Setup Req',
  RRC_SETUP_COMPLETE_TX: 'RRC Setup Cmpl',
  NAS_ATTACH_REQUEST_TX: 'NAS Attach Req',
  NAS_DETACH_TX: 'NAS Detach',
  RRC_RELEASE_TX: 'RRC Release',
}

/** Events rendered as BS->UE arrows in the MSC diagram. */
export const MSC_DOWNLINK: Partial<Record<EventName, string>> = {
  MAC_RACH_MSG2: 'MSG2: RAR',
  MAC_RACH_MSG4: 'MSG4: CR',
  RRC_SETUP_TX: 'RRC Setup',
  NAS_ATTACH_ACCEPT_TX: 'NAS Attach Accept',
}

export default ev

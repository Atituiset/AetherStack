// Phase-2 UE-to-UE services (voice / video / text through the BS).
// Event names are declared locally as plain strings so the UI compiles and
// runs even before events.ts mirrors them (that file is owned by the backend
// agent); only the wire strings matter, never the constant identity.

import { LogEvent } from './hooks/useWebSocket'
import { nodeOf, NODE_LABEL, UeId, UE_NODES, isUe } from './nodes'

export type { UeId }
export { UE_NODES }

export const svc = {
  APP_MSG_TX: 'APP_MSG_TX', // sender UE {dst, text}
  APP_MSG_RX: 'APP_MSG_RX', // receiver UE {src, text}
  APP_CALL_START: 'APP_CALL_START', // caller UE {dst, kind}
  APP_CALL_END: 'APP_CALL_END', // caller UE {dst, kind}
  APP_CALL_INCOMING: 'APP_CALL_INCOMING', // callee UE {src, kind}
  APP_CALL_PEER_END: 'APP_CALL_PEER_END', // callee UE {src, kind}
  APP_FORWARD: 'APP_FORWARD', // BS {src, dst, kind, bytes, count?}
  APP_STREAM_STATS: 'APP_STREAM_STATS', // both UEs {kind, peer, tx, rx, loss, rtt_avg}

  // SIP-lite dialog signaling (P2). Legacy APP_CALL_* events fire alongside:
  // START at INVITE, INCOMING at INVITE receipt, END on local hangup/603,
  // PEER_END on BYE/CANCEL received; STREAM_STATS only while ESTABLISHED.
  SIP_INVITE_TX: 'SIP_INVITE_TX', // caller {dst, kind}
  SIP_INVITE_RX: 'SIP_INVITE_RX', // callee {src, kind}
  SIP_RINGING_TX: 'SIP_RINGING_TX', // callee sent 180 {dst}
  SIP_RINGING_RX: 'SIP_RINGING_RX', // caller got 180 {src}
  SIP_CALL_ESTABLISHED: 'SIP_CALL_ESTABLISHED', // both sides {peer, kind}
  SIP_CALL_FAILED: 'SIP_CALL_FAILED', // {peer, reason}: busy|declined|timeout|unreachable|cancel
  SIP_BYE_TX: 'SIP_BYE_TX', // {peer}
  SIP_BYE_RX: 'SIP_BYE_RX', // {peer}

  // QoS dedicated bearers (M18): strict-priority classes sig > voice > video > BE.
  QOS_BEARER_SETUP: 'QOS_BEARER_SETUP', // UE/BS {c_rnti, qci, kind}
  QOS_BEARER_TEARDOWN: 'QOS_BEARER_TEARDOWN', // UE/BS {c_rnti, qci, kind}

  // 3-party conference (P4): host opens one SIP voice dialog per party with a
  // shared conf_id; the BS audio bridge fans each member's stream (kind=conf)
  // out to every other member.
  CONF_START: 'CONF_START', // {host, conf_id} — host is an IMSI
  CONF_JOIN: 'CONF_JOIN', // {conf_id, imsi}
  CONF_LEAVE: 'CONF_LEAVE', // {conf_id, imsi, reason}: hangup|busy|decline|cancel|detach
  CONF_END: 'CONF_END', // {conf_id, reason}: host|empty|no-parties

  // Link adaptation / power control / signal quality (M19).
  CQI_REPORT: 'CQI_REPORT', // BS {c_rnti, cqi, snr_db} — change-only
  MCS_CHANGE: 'MCS_CHANGE', // BS {c_rnti, mcs, direction}
  TX_POWER_CHANGE: 'TX_POWER_CHANGE', // UE {c_rnti, dbm} — throttled
  LINK_QUALITY: 'LINK_QUALITY', // UE {c_rnti, rsrp, sinr} — ~1/s

  // RRC Inactive + fast resume (M20/P6) + paging (M14, {imsi}).
  RRC_INACTIVE: 'RRC_INACTIVE', // UE/BS {c_rnti, resume_id}
  RRC_RESUME_REQUEST: 'RRC_RESUME_REQUEST', // UE {resume_id} / BS {resume_id, c_rnti}
  RRC_RESUMED: 'RRC_RESUMED', // UE/BS {c_rnti, old_c_rnti}
  RRC_RESUME_FAIL: 'RRC_RESUME_FAIL', // BS {resume_id, reason} / UE {reason}
  PAGE_TX: 'PAGE_TX', // BS {imsi}
  PAGE_RX: 'PAGE_RX', // UE {imsi}

  // 5G-AKA authentication (P7 planned names; M15 names tolerated alongside).
  NAS_AUTH_VECTOR: 'NAS_AUTH_VECTOR', // BS {imsi, rand, sqn_masked}
  NAS_AUTH_RES: 'NAS_AUTH_RES', // UE {imsi, res}
  NAS_AUTH_SUCCESS: 'NAS_AUTH_SUCCESS', // {imsi}
  NAS_AUTH_FAIL: 'NAS_AUTH_FAIL', // P7 {imsi, cause} / M15 {tmsi}
  NAS_AUTH_CHALLENGE_TX: 'NAS_AUTH_CHALLENGE_TX', // M15 BS {imsi, tmsi}
  NAS_AUTH_RESPONSE_TX: 'NAS_AUTH_RESPONSE_TX', // M15 UE {}
  NAS_AUTH_OK: 'NAS_AUTH_OK', // M15 {imsi}

  // Dual-BS mobility / handover (P8 planned names; M14 names tolerated).
  HANDOVER_START: 'HANDOVER_START', // {imsi, from, to} — cells "1"/"2"
  HANDOVER_DONE: 'HANDOVER_DONE', // {imsi, from, to, path}: path ho|reest
  HO_TRIGGERED: 'HO_TRIGGERED', // M14 {from_cell, to_cell} (no UE identity)
  HO_COMMAND_TX: 'HO_COMMAND_TX', // M14 {cell, rnti}
  HO_COMPLETE_RX: 'HO_COMPLETE_RX', // M14 {cell, rnti}
  MEAS_REPORT_TX: 'MEAS_REPORT_TX', // UE {serving, n}
} as const

export type ServiceKind = 'voice' | 'video' | 'msg' | 'conf'

export const KIND_COLOR: Record<ServiceKind, string> = {
  voice: '#38bdf8',
  video: '#f472b6',
  msg: '#fbbf24',
  conf: '#2dd4bf', // teal — distinct from voice blue
}

/** Short Chinese label per kind, for compact stats/caption rendering. */
export const KIND_SHORT: Record<ServiceKind, string> = {
  voice: '语音',
  video: '视频',
  msg: '消息',
  conf: '多方',
}

/** Tolerant kind parse: unknown/missing values fall back. */
export function kindOf(fields: Record<string, string>, fallback: ServiceKind): ServiceKind {
  const k = (fields.kind || '').toLowerCase()
  return k === 'voice' || k === 'video' || k === 'msg' || k === 'conf' ? k : fallback
}

export interface StreamStats {
  tx: string
  rx: string
  loss: string
  rtt: string
  /** Bearer QCI ("1" voice / "2" video / "9" BE); absent on legacy/mock streams. */
  qci?: string
}

export interface CallState {
  kind: ServiceKind
  /** Peer IMSI — the "phone number" on the other end. */
  peer: string
  role: 'caller' | 'callee'
  /** Epoch ms of the event that (re)opened the call, for the elapsed timer. */
  startedAt: number
  /** True once the dialog is established (SIP 200/ACK) or media stats flow. */
  established: boolean
  stats: StreamStats | null
}

export interface ChatMessage {
  id: string
  from: UeId | null
  to: UeId | null
  fromLabel: string
  toLabel: string
  text: string
  time: string
  seq: number
}

/** Build a fully-keyed per-UE record. */
export function ueRecord<V>(init: () => V): Record<UeId, V> {
  return { ue1: init(), ue2: init(), ue3: init() }
}

export function shortImsi(imsi?: string): string {
  if (!imsi) return '?'
  return imsi.length > 6 ? `…${imsi.slice(-4)}` : imsi
}

export function imsiToUe(ueImsi: Record<UeId, string | null>, imsi?: string): UeId | null {
  if (!imsi) return null
  for (const id of UE_NODES) {
    if (ueImsi[id] === imsi) return id
  }
  return null
}

/** Display label for an IMSI: UE name when resolvable, else a short tail. */
export function peerLabel(ueImsi: Record<UeId, string | null>, imsi?: string): string {
  const ue = imsiToUe(ueImsi, imsi)
  return ue ? NODE_LABEL[ue] : shortImsi(imsi)
}

/**
 * Per-UE call state, re-derived by an ordered scan of the window. One UE may
 * hold several concurrent dialogs (e.g. a video call AND a voice call), so
 * calls are keyed by (ue, kind): `out[ue].voice` / `out[ue].video`.
 * - CALL_START / CALL_INCOMING (re)open that kind's call — a START while the
 *   same kind is active is a restart; other kinds are untouched.
 * - CALL_END / CALL_PEER_END close their own kind only. BYE carries no kind,
 *   so it closes the call(s) whose peer matches; with a single open call the
 *   peer is not checked (legacy fallback).
 * - SIP_CALL_FAILED without a kind closes the pending (not yet established)
 *   attempt, leaving established calls of other kinds alone.
 * - STREAM_STATS refresh the live counters per kind (and carry `qci` on
 *   QoS backends). Conference streams report `kind="conf"` and key their own
 *   CallMap slot, separate from the underlying per-leg `voice` dialog. If the
 *   START event slid out of the 500-event window but stats keep streaming and
 *   no END for that kind was seen inside the window, the call is kept alive
 *   from stats alone.
 * - CONF_LEAVE closes the leaver's `conf` slot (the leaver is named by IMSI,
 *   so pass `ueImsi` when available; otherwise the event's own node is used).
 *   CONF_END closes `conf` on every UE and blocks stats-revival.
 * Pairing is IMSI-based, so any UE pair works.
 */
export type CallMap = Record<UeId, Partial<Record<ServiceKind, CallState>>>

export function deriveCalls(messages: LogEvent[], ueImsi?: Record<UeId, string | null>): CallMap {
  const out = ueRecord<Partial<Record<ServiceKind, CallState>>>(() => ({}))
  const lastStats = ueRecord<Partial<Record<ServiceKind, StreamStats>>>(() => ({}))
  const ended = ueRecord<Set<ServiceKind>>(() => new Set())

  const kindsOf = (node: UeId) => Object.keys(out[node]) as ServiceKind[]
  const open = (node: UeId, kind: ServiceKind, peer: string, role: 'caller' | 'callee', ts: number) => {
    ended[node].delete(kind)
    out[node][kind] = {
      kind, peer, role, startedAt: ts,
      stats: lastStats[node][kind] ?? null,
      established: (lastStats[node][kind] ?? null) != null,
    }
  }
  const closeKind = (node: UeId, kind: ServiceKind) => {
    if (out[node][kind]) {
      ended[node].add(kind)
      delete out[node][kind]
    }
  }

  for (const m of messages) {
    const node = nodeOf(m)
    const f = m.fields || {}
    const ts = Date.parse(m.timestamp) || Date.now()

    // Conference membership drives the "conf" CallMap slot's lifecycle. These
    // events may fire on the host / BS, so resolve the leaver by IMSI first.
    if (m.event === svc.CONF_LEAVE) {
      const ue = (ueImsi ? imsiToUe(ueImsi, f.imsi) : null) ?? (isUe(node) ? node : null)
      if (ue) {
        ended[ue].add('conf') // block stats-revival even if the slot was never opened
        if (out[ue].conf) delete out[ue].conf
      }
      continue
    }
    if (m.event === svc.CONF_END) {
      for (const id of UE_NODES) {
        ended[id].add('conf')
        if (out[id].conf) delete out[id].conf
      }
      continue
    }
    if (!isUe(node)) continue

    if (m.event === svc.APP_CALL_START || m.event === svc.SIP_INVITE_TX) {
      open(node, kindOf(f, 'voice'), f.dst || '?', 'caller', ts)
    } else if (m.event === svc.APP_CALL_INCOMING || m.event === svc.SIP_INVITE_RX) {
      open(node, kindOf(f, 'voice'), f.src || '?', 'callee', ts)
    } else if (m.event === svc.SIP_CALL_ESTABLISHED) {
      // media starts now; also revives a call whose INVITE slid out of window
      let kind: ServiceKind
      if (f.kind) {
        kind = kindOf(f, 'voice')
      } else {
        const pend = kindsOf(node).filter((k) => !out[node][k]!.established)
        const all = kindsOf(node)
        kind = pend.length === 1 ? pend[0] : all.length === 1 ? all[0] : 'voice'
      }
      ended[node].delete(kind)
      if (out[node][kind]) out[node][kind] = { ...out[node][kind]!, kind, established: true }
      else out[node][kind] = { kind, peer: f.peer || '?', role: 'caller', startedAt: ts, stats: lastStats[node][kind] ?? null, established: true }
    } else if (m.event === svc.SIP_CALL_FAILED) {
      // busy | declined | timeout | unreachable | cancel — attempt is over.
      // Kill only the failed attempt: an established call of another kind
      // (concurrent dialogs) survives a failed second call.
      if (f.kind) {
        closeKind(node, kindOf(f, 'voice'))
      } else {
        const pend = kindsOf(node).filter((k) => !out[node][k]!.established)
        const targets = pend.length > 0 ? pend : kindsOf(node)
        if (targets.length > 0) {
          for (const k of targets) closeKind(node, k)
        } else {
          // nothing open in-window: still block stats-revival for both kinds
          ended[node].add('voice')
          ended[node].add('video')
        }
      }
    } else if (m.event === svc.SIP_BYE_TX || m.event === svc.SIP_BYE_RX) {
      // BYE names only the peer: close the dialog(s) with that peer. Single
      // open call → close it regardless (peer may have slid out of window).
      const all = kindsOf(node)
      const match = all.filter((k) => out[node][k]!.peer === f.peer)
      const targets = match.length > 0 ? match : all.length === 1 ? all : []
      for (const k of targets) closeKind(node, k)
    } else if (m.event === svc.APP_CALL_END || m.event === svc.APP_CALL_PEER_END) {
      const kind = kindOf(f, 'voice')
      ended[node].add(kind)
      if (out[node][kind]) delete out[node][kind]
    } else if (m.event === svc.APP_STREAM_STATS) {
      const kind = kindOf(f, 'voice')
      const st: StreamStats = { tx: f.tx ?? '-', rx: f.rx ?? '-', loss: f.loss ?? '-', rtt: f.rtt_avg ?? '-' }
      if (f.qci) st.qci = f.qci
      lastStats[node][kind] = st
      if (out[node][kind]) {
        out[node][kind] = { ...out[node][kind]!, stats: st, established: true }
      } else if (!ended[node].has(kind)) {
        out[node][kind] = { kind, peer: f.peer || '?', role: 'caller', startedAt: ts, stats: st, established: true }
      }
    }
  }
  return out
}

/**
 * Chat log from APP_MSG_TX (authoritative, sender side). APP_MSG_RX only adds
 * an entry when no matching TX was seen (backends that emit RX only), so a
 * message forwarded through the BS appears exactly once, in arrival order.
 * Works for any sender/receiver pair — pairing is IMSI-based.
 */
export function deriveChat(messages: LogEvent[], ueImsi: Record<UeId, string | null>): ChatMessage[] {
  const out: ChatMessage[] = []
  const seen = new Set<string>()

  for (const m of messages) {
    const node = nodeOf(m)
    if (!isUe(node)) continue
    const f = m.fields || {}
    const time = m.timestamp ? m.timestamp.split('T')[1]?.replace('Z', '') ?? '' : ''

    if (m.event === svc.APP_MSG_TX) {
      const to = imsiToUe(ueImsi, f.dst)
      const key = `${node}>${to ?? f.dst}:${f.text}`
      if (seen.has(key)) continue
      seen.add(key)
      out.push({
        id: key, from: node, to,
        fromLabel: NODE_LABEL[node], toLabel: to ? NODE_LABEL[to] : shortImsi(f.dst),
        text: f.text || '', time, seq: m._seq ?? 0,
      })
    } else if (m.event === svc.APP_MSG_RX) {
      const from = imsiToUe(ueImsi, f.src)
      const key = `${from ?? f.src}>${node}:${f.text}`
      if (seen.has(key)) continue
      seen.add(key)
      out.push({
        id: key, from, to: node,
        fromLabel: from ? NODE_LABEL[from] : shortImsi(f.src), toLabel: NODE_LABEL[node],
        text: f.text || '', time, seq: m._seq ?? 0,
      })
    }
  }
  return out.slice(-50)
}

/* ------------------------------------------------------------------ */
/* QoS dedicated bearers (M18)                                         */
/* ------------------------------------------------------------------ */

/** Bearer service class for the link pips: sig / voice / video / best-effort. */
export type BearerClass = 'sig' | 'voice' | 'video' | 'be'

export const BEARER_COLOR: Record<BearerClass, string> = {
  sig: '#9ca3af',
  voice: '#38bdf8',
  video: '#f472b6',
  be: '#34d399',
}

/** Display order for bearer pips (scheduling priority, high → low). */
export const BEARER_ORDER: BearerClass[] = ['sig', 'voice', 'video', 'be']

/** QCI badge text/color for stream stats (QCI1 语音 / QCI2 视频 / QCI9 尽力而为). */
export const QCI_INFO: Record<string, { label: string; color: string }> = {
  '1': { label: 'QCI1 语音', color: BEARER_COLOR.voice },
  '2': { label: 'QCI2 视频', color: BEARER_COLOR.video },
  '5': { label: 'QCI5 信令', color: BEARER_COLOR.sig },
  '9': { label: 'QCI9 尽力而为', color: BEARER_COLOR.be },
}

/** Tolerant bearer-class parse: prefer qci, fall back to kind, else null. */
export function bearerClassOf(fields: Record<string, string>): BearerClass | null {
  const qci = (fields.qci || '').trim()
  if (qci === '5') return 'sig'
  if (qci === '1') return 'voice'
  if (qci === '2') return 'video'
  if (qci === '9') return 'be'
  const k = (fields.kind || '').toLowerCase()
  if (k === 'sig') return 'sig'
  if (k === 'voice') return 'voice'
  if (k === 'video') return 'video'
  return null
}

/**
 * Active dedicated bearers per UE from QOS_BEARER_SETUP/TEARDOWN, ordered by
 * class priority. UE-side events key off the node; BS-side events only carry
 * c_rnti, so they are mapped through the known per-UE c_rnti table (unknown
 * c_rnti → dropped). Window-tolerant: a teardown with no in-window setup is a
 * no-op, and a setup whose teardown slid out of the window simply stays up.
 */
export function deriveBearers(messages: LogEvent[], ueCrnti: Record<UeId, string>): Record<UeId, BearerClass[]> {
  const active = ueRecord<Set<BearerClass>>(() => new Set())
  for (const m of messages) {
    if (m.event !== svc.QOS_BEARER_SETUP && m.event !== svc.QOS_BEARER_TEARDOWN) continue
    const f = m.fields || {}
    const cls = bearerClassOf(f)
    if (!cls) continue
    const node = nodeOf(m)
    let ue: UeId | null = null
    if (isUe(node)) ue = node
    else if (node === 'bs' && f.c_rnti) {
      for (const id of UE_NODES) {
        if (ueCrnti[id] !== '-' && ueCrnti[id] === f.c_rnti) { ue = id; break }
      }
    }
    if (!ue) continue
    if (m.event === svc.QOS_BEARER_SETUP) active[ue].add(cls)
    else active[ue].delete(cls)
  }
  const out = ueRecord<BearerClass[]>(() => [])
  for (const id of UE_NODES) {
    out[id] = BEARER_ORDER.filter((c) => active[id].has(c))
  }
  return out
}

/* ------------------------------------------------------------------ */
/* 3-party conference (P4)                                             */
/* ------------------------------------------------------------------ */

export interface ConfMember {
  present: boolean
  /** hangup | busy | decline | cancel | detach — set when the member left. */
  reason?: string
}

export interface ConfState {
  id: string
  /** Host IMSI ('' until CONF_START is seen). */
  host: string
  /** Members keyed by IMSI, in first-seen order (host first on CONF_START). */
  members: Record<string, ConfMember>
  active: boolean
  /** host | empty | no-parties — set by CONF_END. */
  endReason: string
  startedAt: number
}

/**
 * Conference state from CONF_* events, keyed by conf_id. Window-tolerant:
 * a JOIN before its START still creates the conference (host filled in when
 * START arrives); LEAVE/END for an unknown conf_id are dropped (unknown = no
 * conference); a START/JOIN reactivates an id whose END slid out of order.
 */
export function deriveConfs(messages: LogEvent[]): ConfState[] {
  const byId = new Map<string, ConfState>()
  const get = (id: string, ts: number): ConfState => {
    let c = byId.get(id)
    if (!c) {
      c = { id, host: '', members: {}, active: true, endReason: '', startedAt: ts }
      byId.set(id, c)
    }
    return c
  }
  for (const m of messages) {
    const f = m.fields || {}
    const ts = Date.parse(m.timestamp) || Date.now()
    if (m.event === svc.CONF_START) {
      const c = get(f.conf_id || '?', ts)
      if (f.host) c.host = f.host
      c.active = true
      c.endReason = ''
      if (c.host) c.members[c.host] = { present: true }
    } else if (m.event === svc.CONF_JOIN) {
      const c = get(f.conf_id || '?', ts)
      if (f.imsi) c.members[f.imsi] = { present: true }
      c.active = true
    } else if (m.event === svc.CONF_LEAVE) {
      const c = byId.get(f.conf_id || '?')
      if (!c) continue
      if (f.imsi) c.members[f.imsi] = { present: false, reason: f.reason || '' }
    } else if (m.event === svc.CONF_END) {
      const c = byId.get(f.conf_id || '?')
      if (!c) continue
      c.active = false
      c.endReason = f.reason || ''
      for (const imsi of Object.keys(c.members)) {
        c.members[imsi] = { ...c.members[imsi], present: false }
      }
    }
  }
  return [...byId.values()]
}

/** UEs currently present in a conference (IMSI-resolved; unknown IMSIs skipped). */
export function confUeIds(conf: ConfState, ueImsi: Record<UeId, string | null>): UeId[] {
  return UE_NODES.filter((id) => {
    const imsi = ueImsi[id]
    return imsi != null && conf.members[imsi]?.present === true
  })
}

/* ------------------------------------------------------------------ */
/* Link adaptation / TX power / signal quality (M19)                   */
/* ------------------------------------------------------------------ */

/** Latest per-UE radio telemetry; every field null when never seen. */
export interface RadioInfo {
  /** Latest LINK_QUALITY SINR (dB) — the differentiating metric in this sim. */
  sinr: number | null
  /** Latest LINK_QUALITY RSRP (dBm). */
  rsrp: number | null
  /** Latest TX_POWER_CHANGE value (dBm). */
  txDbm: number | null
  /** Latest MCS_CHANGE value, raw lowercase ("qpsk" | "16qam" | "64qam"). */
  mcs: string | null
  /** Latest CQI_REPORT value (1..15). */
  cqi: number | null
}

function parseNum(v: string | undefined): number | null {
  if (v == null || v === '') return null
  const n = Number(v)
  return Number.isFinite(n) ? n : null
}

/**
 * Latest radio state per UE from LINK_QUALITY / TX_POWER_CHANGE (UE-side) and
 * MCS_CHANGE / CQI_REPORT (BS-side, c_rnti-mapped like the bearer events).
 * Window-tolerant: a plain ordered scan, latest value wins; UEs whose events
 * slid out of the window (or older backends) simply stay all-null and the
 * views render no bars/badges.
 */
export function deriveRadio(messages: LogEvent[], ueCrnti: Record<UeId, string>): Record<UeId, RadioInfo> {
  const out = ueRecord<RadioInfo>(() => ({ sinr: null, rsrp: null, txDbm: null, mcs: null, cqi: null }))
  const resolve = (m: LogEvent): UeId | null => {
    const node = nodeOf(m)
    if (isUe(node)) return node
    const cr = m.fields?.c_rnti
    if (cr) {
      for (const id of UE_NODES) {
        if (ueCrnti[id] !== '-' && ueCrnti[id] === cr) return id
      }
    }
    return null
  }
  for (const m of messages) {
    const f = m.fields || {}
    if (m.event === svc.LINK_QUALITY) {
      const ue = resolve(m)
      if (!ue) continue
      const sinr = parseNum(f.sinr)
      const rsrp = parseNum(f.rsrp)
      if (sinr != null) out[ue].sinr = sinr
      if (rsrp != null) out[ue].rsrp = rsrp
    } else if (m.event === svc.TX_POWER_CHANGE) {
      const ue = resolve(m)
      const dbm = ue ? parseNum(f.dbm) : null
      if (ue && dbm != null) out[ue].txDbm = dbm
    } else if (m.event === svc.MCS_CHANGE) {
      const ue = resolve(m)
      if (ue && f.mcs) out[ue].mcs = f.mcs.toLowerCase()
    } else if (m.event === svc.CQI_REPORT) {
      const ue = resolve(m)
      const cqi = ue ? parseNum(f.cqi) : null
      if (ue && cqi != null) out[ue].cqi = cqi
    }
  }
  return out
}

/**
 * Signal bars (0..4) from SINR dB. Demo links sit at ~28/21/19 dB, so the
 * thresholds split them 4/3/2; unknown SINR → 0 (views render "no bars").
 */
export function sinrBars(sinr: number | null): number {
  if (sinr == null) return 0
  if (sinr >= 25) return 4
  if (sinr >= 20) return 3
  if (sinr >= 15) return 2
  if (sinr >= 8) return 1
  return 0
}

/** Quiet quality tier for link flavor: good >= 25, mid >= 20, else poor. */
export function qualityTier(sinr: number | null): 'good' | 'mid' | 'poor' | null {
  if (sinr == null) return null
  return sinr >= 25 ? 'good' : sinr >= 20 ? 'mid' : 'poor'
}

/** MCS badge label ("QPSK" | "16QAM" | "64QAM"); null when never reported. */
export function mcsLabel(mcs: string | null): string | null {
  return mcs ? mcs.toUpperCase() : null
}

/** Badge color: higher-order modulation gets a distinct violet, QPSK stays cool gray. */
export function mcsColor(mcs: string | null): string {
  return mcs === '16qam' || mcs === '64qam' ? '#a78bfa' : '#94a3b8'
}

/* ------------------------------------------------------------------ */
/* RRC Inactive + fast resume (M20/P6)                                 */
/* ------------------------------------------------------------------ */

/** Per-UE sleep/wake state; all zeros/false when no M20 events were seen. */
export interface SleepInfo {
  /** UE is suspended (RRC_INACTIVE, context kept, NAS stays REGISTERED). */
  inactive: boolean
  /** Epoch ms of the latest suspend event (0 = never seen in window). */
  inactiveAt: number
  /** A resume is in flight (RRC_RESUME_REQUEST seen, no RESUMED/FAIL yet). */
  resuming: boolean
  /** Epoch ms of the latest RRC_RESUMED (drives the wake pulse/caption). */
  resumedAt: number
  /** Epoch ms of the latest paging event targeting this UE. */
  pagedAt: number
}

/**
 * Sleep/wake state per UE from RRC_INACTIVE / RRC_RESUME_REQUEST /
 * RRC_RESUMED / RRC_RESUME_FAIL and PAGE_TX/PAGE_RX. UE-side events resolve by
 * node, BS-side ones via the c_rnti table (paging via the IMSI map).
 * RRC_UE_STATE transitions to/from INACTIVE are honored too, so a resumed UE
 * never stays dim even if its RRC_RESUMED slid out of the window. Plain
 * ordered scan — unknown simply means "not inactive".
 */
export function deriveSleep(
  messages: LogEvent[],
  ueCrnti: Record<UeId, string>,
  ueImsi: Record<UeId, string | null>,
): Record<UeId, SleepInfo> {
  const out = ueRecord<SleepInfo>(() => ({ inactive: false, inactiveAt: 0, resuming: false, resumedAt: 0, pagedAt: 0 }))
  const resolve = (m: LogEvent): UeId | null => {
    const node = nodeOf(m)
    if (isUe(node)) return node
    const cr = m.fields?.c_rnti
    if (cr) {
      for (const id of UE_NODES) {
        if (ueCrnti[id] !== '-' && ueCrnti[id] === cr) return id
      }
    }
    return null
  }
  for (const m of messages) {
    const f = m.fields || {}
    const ts = Date.parse(m.timestamp) || Date.now()
    if (m.event === svc.RRC_INACTIVE) {
      const ue = resolve(m)
      if (!ue) continue
      out[ue] = { ...out[ue], inactive: true, inactiveAt: ts, resuming: false }
    } else if (m.event === svc.RRC_RESUME_REQUEST) {
      const ue = resolve(m)
      if (ue && out[ue].inactive) out[ue].resuming = true
    } else if (m.event === svc.RRC_RESUMED) {
      const ue = resolve(m)
      if (!ue) continue
      out[ue] = { ...out[ue], inactive: false, resuming: false, resumedAt: ts }
    } else if (m.event === svc.RRC_RESUME_FAIL) {
      // BS-side form carries only resume_id: attribute to the single UE with
      // a resume in flight, if there is exactly one.
      const ue = resolve(m)
      if (ue) out[ue].resuming = false // resume failed: still inactive
      else {
        const resumers = UE_NODES.filter((id) => out[id].resuming)
        if (resumers.length === 1) out[resumers[0]].resuming = false
      }
    } else if (m.event === 'RRC_UE_STATE') {
      const ue = resolve(m)
      if (!ue) continue
      const next = f.new || f.new_state
      if (next === 'INACTIVE') {
        out[ue] = { ...out[ue], inactive: true, inactiveAt: out[ue].inactiveAt || ts, resuming: false }
      } else if (next === 'CONNECTED') {
        out[ue] = { ...out[ue], inactive: false, resuming: false }
      }
    } else if (m.event === svc.PAGE_TX || m.event === svc.PAGE_RX) {
      const node = nodeOf(m)
      const ue = isUe(node) ? node : imsiToUe(ueImsi, f.imsi)
      if (ue) out[ue].pagedAt = ts
    }
  }
  return out
}

/* ------------------------------------------------------------------ */
/* 5G-AKA authentication (P7; M15 names tolerated)                     */
/* ------------------------------------------------------------------ */

export type AuthStep = 'vector' | 'res' | 'success' | 'fail'

/** Latest AKA exchange state per UE; all null/0 when no auth events seen. */
export interface AuthInfo {
  /** Latest step of the exchange (vector = challenge, res = response). */
  step: AuthStep | null
  /** P7 failure cause: mac | synch | res_mismatch ('' when not provided). */
  cause: string
  /** Epoch ms of the latest auth event. */
  at: number
  /** Epoch ms of the latest success / failure (caption freshness windows). */
  okAt: number
  failAt: number
}

/** Chinese caption text per NAS_AUTH_FAIL cause. */
export const AUTH_CAUSE: Record<string, string> = {
  mac: 'MAC 校验失败',
  synch: 'SQN 失步',
  res_mismatch: 'RES 不匹配',
}

/**
 * AKA exchange state per UE, tolerating both the P7 event family
 * (NAS_AUTH_VECTOR/RES/SUCCESS/FAIL) and the older M15 one
 * (NAS_AUTH_CHALLENGE_TX/RESPONSE_TX/OK/FAIL). Resolution: UE node first,
 * then the IMSI field; field-less BS events are attributed to the single UE
 * with an exchange in flight (same heuristic as RRC_RESUME_FAIL). A fresh
 * challenge resets any terminal state, so a fail→retry→success sequence
 * reads correctly; window-tolerant as usual (unknown = no auth seen).
 */
export function deriveAuth(messages: LogEvent[], ueImsi: Record<UeId, string | null>): Record<UeId, AuthInfo> {
  const out = ueRecord<AuthInfo>(() => ({ step: null, cause: '', at: 0, okAt: 0, failAt: 0 }))
  const resolve = (m: LogEvent): UeId | null => {
    const node = nodeOf(m)
    if (isUe(node)) return node
    const ue = imsiToUe(ueImsi, m.fields?.imsi)
    if (ue) return ue
    const inflight = UE_NODES.filter((id) => out[id].step === 'vector' || out[id].step === 'res')
    return inflight.length === 1 ? inflight[0] : null
  }
  for (const m of messages) {
    const f = m.fields || {}
    const ts = Date.parse(m.timestamp) || Date.now()
    if (m.event === svc.NAS_AUTH_VECTOR || m.event === svc.NAS_AUTH_CHALLENGE_TX) {
      const ue = resolve(m)
      if (!ue) continue
      out[ue] = { ...out[ue], step: 'vector', cause: '', at: ts } // new attempt resets terminal state
    } else if (m.event === svc.NAS_AUTH_RES || m.event === svc.NAS_AUTH_RESPONSE_TX) {
      const ue = resolve(m)
      if (!ue) continue
      out[ue] = { ...out[ue], step: 'res', at: ts }
    } else if (m.event === svc.NAS_AUTH_SUCCESS || m.event === svc.NAS_AUTH_OK) {
      const ue = resolve(m)
      if (!ue) continue
      out[ue] = { ...out[ue], step: 'success', at: ts, okAt: ts }
    } else if (m.event === svc.NAS_AUTH_FAIL) {
      const ue = resolve(m)
      if (!ue) continue
      out[ue] = { ...out[ue], step: 'fail', cause: f.cause || '', at: ts, failAt: ts }
    }
  }
  return out
}

/* ------------------------------------------------------------------ */
/* Dual-BS mobility / handover (P8; M14 names tolerated)               */
/* ------------------------------------------------------------------ */

/** Per-UE serving-cell + handover state; defaults: cell 1, no handover. */
export interface MobilityInfo {
  /** Serving cell id: '1' | '2' (default '1' — single-BS streams never change). */
  serving: string
  /** A handover is in flight (START seen, no DONE yet). */
  inFlight: boolean
  /** Source/target cell of the latest handover ('' when none). */
  hoFrom: string
  hoTo: string
  /** ho | reest — from HANDOVER_DONE ('' until then). */
  hoPath: string
  hoStartAt: number
  hoDoneAt: number
}

/** Tolerant cell-id parse: "1"/"2"/"bs"/"bs2"/"gnb1"/"gnb2" → '1' | '2' | null. */
export function cellOf(v?: string): string | null {
  const s = (v || '').toLowerCase().trim()
  if (s === '1' || s === 'bs' || s === 'bs1' || s === 'gnb' || s === 'gnb1') return '1'
  if (s === '2' || s === 'bs2' || s === 'gnb2') return '2'
  return null
}

/**
 * Serving cell + handover state per UE. Primary signal: HANDOVER_START/DONE
 * (IMSI-keyed, P8). The M14 flow is tolerated too: HO_COMMAND_TX {cell, rnti}
 * opens the in-flight state and HO_COMPLETE_RX {cell, rnti} completes it
 * (rnti-mapped, with a single-in-flight fallback since a fresh rnti may not
 * be known yet). HO_TRIGGERED carries no UE identity and is ignored unless it
 * names one. Default serving cell is '1'; a stream with no mobility events
 * (single-BS backend) leaves everything at defaults and renders as before.
 */
export function deriveMobility(
  messages: LogEvent[],
  ueImsi: Record<UeId, string | null>,
  ueCrnti: Record<UeId, string>,
): Record<UeId, MobilityInfo> {
  const out = ueRecord<MobilityInfo>(() => ({
    serving: '1', inFlight: false, hoFrom: '', hoTo: '', hoPath: '', hoStartAt: 0, hoDoneAt: 0,
  }))
  const resolve = (m: LogEvent): UeId | null => {
    const node = nodeOf(m)
    if (isUe(node)) return node
    const byImsi = imsiToUe(ueImsi, m.fields?.imsi)
    if (byImsi) return byImsi
    const cr = m.fields?.rnti || m.fields?.c_rnti
    if (cr) {
      for (const id of UE_NODES) {
        if (ueCrnti[id] !== '-' && ueCrnti[id] === cr) return id
      }
    }
    const inflight = UE_NODES.filter((id) => out[id].inFlight)
    return inflight.length === 1 ? inflight[0] : null
  }
  for (const m of messages) {
    const f = m.fields || {}
    const ts = Date.parse(m.timestamp) || Date.now()
    if (m.event === svc.HANDOVER_START) {
      const ue = resolve(m)
      if (!ue) continue
      const from = cellOf(f.from) ?? out[ue].serving
      const to = cellOf(f.to)
      if (!to) continue
      out[ue] = { ...out[ue], serving: from, inFlight: true, hoFrom: from, hoTo: to, hoStartAt: ts }
    } else if (m.event === svc.HANDOVER_DONE) {
      const ue = resolve(m)
      if (!ue) continue
      const to = cellOf(f.to) ?? out[ue].hoTo
      if (!to) continue
      out[ue] = {
        ...out[ue], serving: to, inFlight: false, hoTo: to,
        hoFrom: cellOf(f.from) ?? out[ue].hoFrom,
        hoPath: (f.path || 'ho').toLowerCase(), hoDoneAt: ts,
      }
    } else if (m.event === svc.HO_COMMAND_TX) {
      const ue = resolve(m)
      const cell = cellOf(f.cell)
      if (!ue || !cell) continue
      out[ue] = { ...out[ue], inFlight: true, hoFrom: out[ue].serving, hoTo: cell, hoStartAt: ts }
    } else if (m.event === svc.HO_COMPLETE_RX) {
      const ue = resolve(m)
      const cell = cellOf(f.cell)
      if (!ue || !cell) continue
      out[ue] = { ...out[ue], serving: cell, inFlight: false, hoTo: cell, hoPath: out[ue].hoPath || 'ho', hoDoneAt: ts }
    }
  }
  return out
}

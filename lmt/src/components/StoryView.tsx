import React, { useCallback, useEffect, useRef, useState } from 'react'
import ev from '../events'
import { LogEvent } from '../hooks/useWebSocket'
import { NodeId, nodeOf, NODE_LABEL, NODE_COLOR, UE_NODES, isBs } from '../nodes'
import {
  svc, kindOf, imsiToUe, peerLabel, shortImsi, KIND_COLOR, KIND_SHORT, QCI_INFO, BEARER_COLOR,
  BearerClass, ServiceKind, CallMap, CallState, ChatMessage, ConfState, RadioInfo, SleepInfo,
  AuthInfo, AUTH_CAUSE, MobilityInfo, UeId,
  confUeIds, sinrBars, qualityTier, mcsLabel, mcsColor, cellOf, ueRecord,
} from '../services'
import SignalBars, { SIGNAL_BARS_WIDTH } from './SignalBars'

interface StoryViewProps {
  messages: LogEvent[]
  presence: Record<NodeId, string>
  ueNas: Record<UeId, string>
  ueImsi: Record<UeId, string | null>
  /** Active calls per UE, keyed by service kind (concurrent voice+video+conf). */
  calls: CallMap
  /** Active QoS bearer classes per UE (link pips). */
  bearers: Record<UeId, BearerClass[]>
  /** Conferences keyed by conf_id (P4 3-party calls via the BS audio bridge). */
  confs: ConfState[]
  /** Latest radio telemetry per UE (SINR/MCS/TX power); nulls when unseen. */
  radio: Record<UeId, RadioInfo>
  /** RRC Inactive / paging state per UE (P6); all-false on older backends. */
  sleep: Record<UeId, SleepInfo>
  /** 5G-AKA exchange state per UE (P7); nulls on older backends. */
  auth: Record<UeId, AuthInfo>
  /** Serving cell + handover state per UE (P8); all cell-1 on single-BS streams. */
  mobility: Record<UeId, MobilityInfo>
  chat: ChatMessage[]
  /** Jump back to the ops dashboard with this node preselected. */
  onInspectNode: (n: NodeId) => void
  /** Send a UE command (answer / decline / call end / autoanswer …). */
  onCommand: (target: NodeId, cmd: string) => void
}

type Phase = 'idle' | 'dialing' | 'ringing' | 'in-call' | 'ended' | 'failed'
type Path = UeId
type Dir = 'ul' | 'dl'

interface Packet {
  id: number
  path: Path
  dir: Dir
  color: string
  r: number
  kind: ServiceKind | null
  /** Special glyphs for the AKA beat: challenge lock / response key. */
  glyph?: 'lock' | 'key'
  /** Override BS endpoint (handover packets target a specific tower). */
  bsAt?: { x: number; y: number }
  t0: number
  dur: number
}

interface PendingHop {
  src: string
  dst: string
  kind: ServiceKind
  landAt: number
}

/**
 * Story-beat flags rebuilt incrementally from the event stream (seq-based).
 * Per (UE, kind), so concurrent voice+video calls on one UE each keep their
 * own media/activity beat and an unrelated third UE stays out of the way.
 */
interface Beat {
  /** UE has an open call of this kind (INVITE seen, no BYE/failure yet). */
  active: Record<UeId, Partial<Record<ServiceKind, boolean>>>
  /** Epoch ms when media of this kind started flowing to/from this UE. */
  mediaAt: Record<UeId, Partial<Record<ServiceKind, number>>>
  /** Caller-side ringback: epoch ms when SIP_RINGING_RX arrived (180). */
  ringbackAt: Record<UeId, number>
  /** Epoch ms of the last hangup event (any UE). */
  endAt: number
  /** Last failed call attempt (SIP_CALL_FAILED), for the terminal beat. */
  fail: { node: UeId; reason: string; at: number } | null
  /** Any SIP_* seen: forwards stop counting as media (signaling uses them too). */
  sipSeen: boolean
  msgFlash: { from: UeId; to: UeId | null; at: number } | null
  /** Last conference membership beat (CONF_*), for the narration caption. */
  confFlash: { type: 'start' | 'join' | 'leave' | 'end'; imsi: string; at: number } | null
}

/* --- scene geometry (viewBox 960x500): UE1 left, UE2 right, UE3 bottom --- */
const UE_C: Record<Path, { x: number; y: number }> = {
  ue1: { x: 150, y: 190 },
  ue2: { x: 810, y: 190 },
  ue3: { x: 480, y: 398 },
}
const PHONE_W = 74
const PHONE_H = 126
const LINK_UE: Record<Path, { x: number; y: number }> = {
  ue1: { x: 150, y: 127 },
  ue2: { x: 810, y: 127 },
  ue3: { x: 480, y: 335 },
}
/** Two towers (P8 dual-BS): BS1 top-left, BS2 top-right. */
const BS_ANT: Record<string, { x: number; y: number }> = {
  '1': { x: 300, y: 96 },
  '2': { x: 660, y: 96 },
}
/** BS end of each link per serving cell: the serving tower's antenna. */
const LINK_BS: Record<Path, Record<string, { x: number; y: number }>> = {
  ue1: { '1': { x: 300, y: 104 }, '2': { x: 660, y: 104 } },
  ue2: { '1': { x: 300, y: 104 }, '2': { x: 660, y: 104 } },
  ue3: { '1': { x: 300, y: 104 }, '2': { x: 660, y: 104 } },
}

const MAX_PACKETS = 48
const SIG_FLIGHT = 800
const MEDIA_DUR: Record<ServiceKind, number> = { voice: 700, video: 950, msg: 900, conf: 520 }
const MEDIA_CADENCE: Record<ServiceKind, number> = { voice: 140, video: 210, msg: 300, conf: 90 }

const STEPS = ['拨号', '振铃', '通话', '挂断']

/** Events too noisy/raw for the story-view inspector. */
const INSPECT_SKIP = new Set<string>([ev.PDU_TRACE, ev.HEARTBEAT])

function fmtElapsed(startedAt: number, now: number): string {
  const s = Math.max(0, Math.floor((now - startedAt) / 1000))
  return `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`
}

export const StoryView: React.FC<StoryViewProps> = ({
  messages, presence, ueNas, ueImsi, calls, bearers, confs, radio, sleep, auth, mobility, chat, onInspectNode, onCommand,
}) => {
  const packetsRef = useRef<Packet[]>([])
  const pendingRef = useRef<PendingHop[]>([])
  const beatRef = useRef<Beat>({
    active: ueRecord(() => ({})),
    mediaAt: ueRecord(() => ({})),
    ringbackAt: ueRecord(() => 0),
    endAt: 0,
    fail: null,
    sipSeen: false,
    msgFlash: null,
    confFlash: null,
  })
  const attachAtRef = useRef<Record<UeId, number>>(ueRecord(() => 0))
  const lastSeqRef = useRef(0)
  const primedRef = useRef(false)
  /** Last cadence spawn per "path:kind" key (each bearer kind keeps its rhythm). */
  const lastCadenceRef = useRef<Record<string, number>>({})
  const idRef = useRef(0)
  const rafRef = useRef<number | null>(null)
  const [, setTick] = useState(0)
  const [inspect, setInspect] = useState<NodeId | null>(null)
  /** Auto-answer toggle per UE (backend default: 4000 ms). */
  const [autoAns, setAutoAns] = useState<Record<UeId, boolean>>({ ue1: true, ue2: true, ue3: true })

  /* mirrors so the rAF loop / event effect always read the latest render state */
  const phaseRef = useRef<Phase>('idle')
  /** (UE link, kind) pairs carrying live media, for the per-kind cadence spawn. */
  const mediaRef = useRef<{ path: Path; kind: ServiceKind }[]>([])
  /** Serving cell per UE link; packets aim at the serving tower. */
  const servingRef = useRef<Record<Path, string>>({ ue1: '1', ue2: '1', ue3: '1' })

  const pushPacket = useCallback((p: Omit<Packet, 'id'>) => {
    if (packetsRef.current.length >= MAX_PACKETS) packetsRef.current.shift()
    packetsRef.current.push({ ...p, id: ++idRef.current })
  }, [])

  const loop = useCallback(() => {
    const now = performance.now()
    // in-call media cadence: dots are spawned on a fixed rhythm so the stream
    // looks alive; real APP_FORWARD arrivals only confirm the beat, they do
    // not drive 1:1 dots during a call (they are ~1/s aggregates). Each live
    // (link, kind) pair animates on its own cadence, so a UE running voice +
    // video concurrently shows both streams; UEs with no call stay idle.
    for (const m of mediaRef.current) {
      const key = `${m.path}:${m.kind}`
      if (now - (lastCadenceRef.current[key] ?? 0) >= MEDIA_CADENCE[m.kind]) {
        lastCadenceRef.current[key] = now
        const dur = MEDIA_DUR[m.kind]
        pushPacket({ path: m.path, dir: 'ul', color: KIND_COLOR[m.kind], r: m.kind === 'video' ? 5 : 3, kind: m.kind, t0: now, dur })
        pushPacket({ path: m.path, dir: 'dl', color: KIND_COLOR[m.kind], r: m.kind === 'video' ? 5 : 3, kind: m.kind, t0: now + dur, dur })
      }
    }
    packetsRef.current = packetsRef.current.filter((p) => now - p.t0 < p.dur)
    setTick((t) => t + 1)
    if (packetsRef.current.length > 0 || mediaRef.current.length > 0) {
      rafRef.current = requestAnimationFrame(loop)
    } else {
      rafRef.current = null
    }
  }, [pushPacket])

  const ensureLoop = useCallback(() => {
    if (rafRef.current == null) rafRef.current = requestAnimationFrame(loop)
  }, [loop])

  useEffect(() => () => {
    if (rafRef.current != null) cancelAnimationFrame(rafRef.current)
  }, [])

  /* Beat flags are rebuilt from ALL events in the window (backlog included),
     so remounts/out-of-order streams still land on the right story phase.
     Only the packet animations are gated to fresh events. */
  useEffect(() => {
    if (messages.length === 0) {
      lastSeqRef.current = 0
      primedRef.current = false
      return
    }
    const now = performance.now()
    pendingRef.current = pendingRef.current.filter((p) => p.landAt > now - 5000)
    const b = beatRef.current

    for (const m of messages) {
      const seq = m._seq ?? 0
      if (seq === 0 || seq <= lastSeqRef.current) continue
      lastSeqRef.current = seq
      const node = nodeOf(m)
      const e = m.event
      const f = m.fields || {}
      const ts = Date.parse(m.timestamp) || Date.now()

      /* --- beat tracking (always) --- */
      if (node === 'ue1' || node === 'ue2' || node === 'ue3') {
        if (e === svc.APP_CALL_START || e === svc.SIP_INVITE_TX) {
          const k = kindOf(f, 'voice')
          b.active[node][k] = true; b.mediaAt[node][k] = 0; b.endAt = 0; b.fail = null
        } else if (e === svc.APP_CALL_INCOMING || e === svc.SIP_INVITE_RX) {
          const k = kindOf(f, 'voice')
          b.active[node][k] = true; b.mediaAt[node][k] = 0
        } else if (e === svc.SIP_RINGING_RX) {
          b.ringbackAt[node] = ts // caller hears the callee ring (180)
        } else if (e === svc.SIP_CALL_ESTABLISHED) {
          const k = kindOf(f, 'voice')
          b.active[node][k] = true; b.mediaAt[node][k] = ts; b.ringbackAt[node] = 0; b.endAt = 0; b.fail = null
        } else if (e === svc.SIP_CALL_FAILED) {
          // no kind on the wire: clear the pending attempt, keep other kinds
          const ks = (Object.keys(b.active[node]) as ServiceKind[]).filter((k) => b.active[node][k])
          const pend = ks.filter((k) => !(b.mediaAt[node][k]! > 0))
          for (const k of pend.length > 0 ? pend : ks) {
            b.active[node][k] = false; b.mediaAt[node][k] = 0
          }
          b.ringbackAt[node] = 0
          b.fail = { node, reason: f.reason || '?', at: ts }
        } else if (e === svc.APP_CALL_END || e === svc.APP_CALL_PEER_END) {
          const k = kindOf(f, 'voice')
          b.active[node][k] = false; b.mediaAt[node][k] = 0; b.ringbackAt[node] = 0; b.endAt = ts
        } else if (e === svc.SIP_BYE_TX || e === svc.SIP_BYE_RX) {
          // BYE names only the peer; calls[] does the precise matching, here
          // clear every kind — a surviving concurrent call re-beats on its
          // next STREAM_STATS (they arrive every few seconds).
          for (const k of Object.keys(b.active[node]) as ServiceKind[]) {
            b.active[node][k] = false; b.mediaAt[node][k] = 0
          }
          b.ringbackAt[node] = 0; b.endAt = ts
        } else if (e === svc.APP_STREAM_STATS) {
          const k = kindOf(f, 'voice')
          b.active[node][k] = true; b.mediaAt[node][k] = ts
        } else if (e === svc.APP_MSG_TX) {
          b.msgFlash = { from: node, to: imsiToUe(ueImsi, f.dst), at: ts }
        } else if ((e === ev.NAS_STATE_CHANGE && (f.new || f.new_state) === 'REGISTERED') || e === ev.NAS_ATTACH_ACCEPT_RX) {
          attachAtRef.current[node] = ts
        }
      }
      if (e.startsWith('SIP_')) b.sipSeen = true
      // Conference membership beats (may fire on host/BS nodes): narrated by
      // the caption; CONF_END also drives the 挂断 step + "会议结束" tail.
      if (e === svc.CONF_START) {
        b.confFlash = { type: 'start', imsi: f.host || '', at: ts }
      } else if (e === svc.CONF_JOIN) {
        b.confFlash = { type: 'join', imsi: f.imsi || '', at: ts }
      } else if (e === svc.CONF_LEAVE) {
        b.confFlash = { type: 'leave', imsi: f.imsi || '', at: ts }
      } else if (e === svc.CONF_END) {
        b.confFlash = { type: 'end', imsi: '', at: ts }
        b.endAt = ts
      }
      if (e === svc.APP_FORWARD) {
        const k = kindOf(f, 'msg')
        // Media from forwards only counts on legacy (pre-SIP) streams: with
        // SIP, signaling (INVITE/180/BYE) is forwarded too and would fake
        // media — there, ESTABLISHED and STREAM_STATS mark media instead.
        if (k !== 'msg' && !b.sipSeen) {
          const src = imsiToUe(ueImsi, f.src)
          const dst = imsiToUe(ueImsi, f.dst)
          if (src && dst && b.active[src][k] && b.active[dst][k]) {
            b.mediaAt[src][k] = ts
            b.mediaAt[dst][k] = ts
          }
        }
      }

      /* --- animations (fresh events only) --- */
      if (!primedRef.current) continue
      if (!node) continue

      if (e === svc.PAGE_TX) {
        // paging wave: BS -> dimmed phone (violet), the P6 money shot
        const dstPath = imsiToUe(ueImsi, f.imsi)
        if (dstPath) pushPacket({ path: dstPath, dir: 'dl', color: '#a78bfa', r: 5, kind: null, t0: now, dur: SIG_FLIGHT })
        continue
      }
      if (e === svc.HANDOVER_START || e === svc.HANDOVER_DONE) {
        // 切换 packet: UE -> old tower, then new tower -> UE
        const ue = imsiToUe(ueImsi, f.imsi) ?? (node !== 'bs' && node !== 'bs2' ? node : null)
        const from = cellOf(f.from)
        const to = cellOf(f.to)
        const color = e === svc.HANDOVER_START ? '#f59e0b' : '#10b981'
        if (ue && from && LINK_BS[ue][from]) {
          pushPacket({ path: ue, dir: 'ul', color, r: 4.5, kind: null, bsAt: LINK_BS[ue][from], t0: now, dur: SIG_FLIGHT })
        }
        if (ue && to && LINK_BS[ue][to]) {
          pushPacket({ path: ue, dir: 'dl', color, r: 4.5, kind: null, bsAt: LINK_BS[ue][to], t0: now + SIG_FLIGHT, dur: SIG_FLIGHT })
        }
        continue
      }
      /* --- 5G-AKA beat: challenge lock flies down, response key flies up --- */
      if (e === svc.NAS_AUTH_VECTOR || e === svc.NAS_AUTH_CHALLENGE_TX) {
        const dstPath = !isBs(node) ? node : imsiToUe(ueImsi, f.imsi)
        if (dstPath) pushPacket({ path: dstPath, dir: 'dl', color: '#f97316', r: 4.5, kind: null, glyph: 'lock', t0: now, dur: SIG_FLIGHT })
        continue
      }
      if (e === svc.NAS_AUTH_RES || e === svc.NAS_AUTH_RESPONSE_TX) {
        const srcPath = !isBs(node) ? node : imsiToUe(ueImsi, f.imsi)
        if (srcPath) pushPacket({ path: srcPath, dir: 'ul', color: '#f97316', r: 4.5, kind: null, glyph: 'key', t0: now, dur: SIG_FLIGHT })
        continue
      }
      if (e === svc.NAS_AUTH_SUCCESS || e === svc.NAS_AUTH_OK || e === svc.NAS_AUTH_FAIL) {
        const dstPath = !isBs(node) ? node : imsiToUe(ueImsi, f.imsi)
        const ok = e !== svc.NAS_AUTH_FAIL
        if (dstPath) pushPacket({ path: dstPath, dir: 'dl', color: ok ? '#10b981' : '#ef4444', r: 4, kind: null, glyph: 'lock', t0: now, dur: SIG_FLIGHT })
        continue
      }

      if (!isBs(node) && (e === svc.APP_CALL_START || e === svc.APP_CALL_END || e === svc.APP_MSG_TX)) {
        const kind: ServiceKind = e === svc.APP_MSG_TX ? 'msg' : kindOf(f, 'voice')
        const color = e === svc.APP_CALL_END ? '#ef4444' : KIND_COLOR[kind]
        if (f.dst) {
          pendingRef.current.push({ src: ueImsi[node] ?? '', dst: f.dst, kind, landAt: now + SIG_FLIGHT })
          if (pendingRef.current.length > 24) pendingRef.current.shift()
        }
        pushPacket({ path: node, dir: 'ul', color, r: 4.5, kind: e === svc.APP_MSG_TX ? 'msg' : null, t0: now, dur: SIG_FLIGHT })
        continue
      }
      if (!isBs(node) && (e === svc.APP_CALL_INCOMING || e === svc.APP_CALL_PEER_END)) {
        const color = e === svc.APP_CALL_PEER_END ? '#ef4444' : '#f59e0b'
        pushPacket({ path: node, dir: 'dl', color, r: 4.5, kind: null, t0: now, dur: SIG_FLIGHT })
        continue
      }
      if (e === svc.APP_FORWARD) {
        const kind = kindOf(f, 'msg')
        const dstPath = imsiToUe(ueImsi, f.dst)
        const srcPath = imsiToUe(ueImsi, f.src)
        const pi = pendingRef.current.findIndex((p) =>
          p.dst === f.dst && p.kind === kind && (!p.src || !f.src || p.src === f.src))
        if (pi >= 0) {
          // correlated call-setup/msg hop: second leg leaves as hop 1 lands
          const t0 = Math.max(now, pendingRef.current[pi].landAt)
          pendingRef.current.splice(pi, 1)
          if (dstPath) pushPacket({ path: dstPath, dir: 'dl', color: KIND_COLOR[kind], r: 4.5, kind: kind === 'msg' ? 'msg' : null, t0, dur: SIG_FLIGHT })
        } else if (kind === 'msg') {
          // text message: fly both hops as a chat-bubble glyph
          const dur = MEDIA_DUR.msg
          if (srcPath) pushPacket({ path: srcPath, dir: 'ul', color: KIND_COLOR.msg, r: 4.5, kind: 'msg', t0: now, dur })
          if (dstPath) pushPacket({ path: dstPath, dir: 'dl', color: KIND_COLOR.msg, r: 4.5, kind: 'msg', t0: now + dur, dur })
        } else if (!(srcPath && dstPath && calls[srcPath]?.[kind] && calls[dstPath]?.[kind])) {
          // orphaned media forward (no active call of this kind on the pair): still show it
          const dur = MEDIA_DUR[kind]
          if (srcPath) pushPacket({ path: srcPath, dir: 'ul', color: KIND_COLOR[kind], r: 4, kind, t0: now, dur })
          if (dstPath) pushPacket({ path: dstPath, dir: 'dl', color: KIND_COLOR[kind], r: 4, kind, t0: now + dur, dur })
        }
        // live-call media forwards: cadence owns the visuals
        continue
      }
    }
    primedRef.current = true
    ensureLoop()
  }, [messages, ueImsi, calls, pushPacket, ensureLoop])

  /* 1 Hz tick for elapsed timers / caption freshness even when idle */
  useEffect(() => {
    const iv = setInterval(() => setTick((t) => t + 1), 1000)
    return () => clearInterval(iv)
  }, [])

  /* ---------- phase / caption derivation (dialog-aware) ---------- */
  const b = beatRef.current
  const nowMs = Date.now()
  // flatten concurrent calls: one entry per (ue, kind)
  const allCalls = UE_NODES.flatMap((id) =>
    (Object.values(calls[id] ?? {}) as CallState[]).map((call) => ({ ue: id, call })),
  )
  const hasMedia = (ue: UeId, call: CallState) => (b.mediaAt[ue][call.kind] ?? 0) > 0 || call.stats != null
  /** (ue, kind) entries whose call has live media (ESTABLISHED/stats). */
  const inCall = allCalls.filter(({ ue, call }) => hasMedia(ue, call))
  // media cadence: one (link, kind) pair each — both ends of every dialog
  // animate, and concurrent kinds on one UE each keep their own rhythm
  {
    const seen = new Set<string>()
    mediaRef.current = []
    for (const { ue, call } of inCall) {
      const key = `${ue}:${call.kind}`
      if (seen.has(key)) continue
      seen.add(key)
      mediaRef.current.push({ path: ue, kind: call.kind })
    }
  }

  // Conference legs open as ordinary voice dialogs, but the conference itself
  // is narrated as one scene — keep conf stream entries out of 1:1 dialogs.
  const callers = allCalls.filter((c) => c.call.role === 'caller' && c.call.kind !== 'conf')
  const callees = allCalls.filter((c) => c.call.role === 'callee' && c.call.kind !== 'conf')

  const failFresh = b.fail && nowMs - b.fail.at < 3000 ? b.fail : null
  const endedFresh = b.endAt > 0 && nowMs - b.endAt < 2500
  const phase: Phase =
    (inCall.length >= 2 || (inCall.length === 1 && allCalls.length >= 2)) ? 'in-call'
      : callees.length > 0 ? 'ringing'
        : allCalls.length > 0 ? 'dialing'
          // a fresh failure beats a stale "ended", and vice versa
          : failFresh && (!endedFresh || failFresh.at >= b.endAt) ? 'failed'
            : endedFresh ? 'ended'
              : 'idle'
  phaseRef.current = phase

  const msgFlashFresh = b.msgFlash && nowMs - b.msgFlash.at < 2500 ? b.msgFlash : null
  const anyRegistered = UE_NODES.some((id) => ueNas[id] === 'REGISTERED')

  // Dialogs = one per caller-side call; callee-side calls only fill in when
  // the caller's half is not visible (slid out of the window).
  interface Dialog { from: UeId | null; to: UeId | null; peer: string; kind: ServiceKind; startedAt: number; live: boolean }
  const dialogs: Dialog[] = [
    ...callers.map(({ ue, call }) => ({
      from: ue as UeId | null, to: imsiToUe(ueImsi, call.peer), peer: call.peer,
      kind: call.kind, startedAt: call.startedAt, live: hasMedia(ue, call),
    })),
    ...callees
      .filter(({ call }) => {
        const peerUe = imsiToUe(ueImsi, call.peer)
        return !peerUe || calls[peerUe]?.[call.kind]?.role !== 'caller'
      })
      .map(({ ue, call }) => ({
        from: imsiToUe(ueImsi, call.peer), to: ue as UeId | null, peer: call.peer,
        kind: call.kind, startedAt: call.startedAt, live: hasMedia(ue, call),
      })),
  ]
  const ueName = (id: UeId | null, peer: string) => (id ? NODE_LABEL[id] : shortImsi(peer))
  // QoS priority scheduling shows when one UE runs voice + video at once
  const qosConcurrent = UE_NODES.some((id) => calls[id]?.voice && calls[id]?.video)

  // active conference + its member UEs (IMSI-resolved)
  const activeConf = confs.find((c) => c.active) ?? null
  const confUes = activeConf ? confUeIds(activeConf, ueImsi) : []
  const confFlashFresh = b.confFlash && nowMs - b.confFlash.at < 2500 ? b.confFlash : null

  // P6: fresh paging target / just-resumed UE (short narration windows)
  const pagedUe = UE_NODES
    .filter((id) => sleep[id].pagedAt > 0 && nowMs - sleep[id].pagedAt < 2500)
    .sort((a, z) => sleep[z].pagedAt - sleep[a].pagedAt)[0] ?? null
  const wakeUe = UE_NODES
    .filter((id) => sleep[id].resumedAt > 0 && nowMs - sleep[id].resumedAt < 2500)
    .sort((a, z) => sleep[z].resumedAt - sleep[a].resumedAt)[0] ?? null

  // P7: AKA exchange beats (only when NAS_AUTH_* events exist at all)
  const authBusyUe = UE_NODES
    .filter((id) => (auth[id].step === 'vector' || auth[id].step === 'res') && nowMs - auth[id].at < 4000)
    .sort((a, z) => auth[z].at - auth[a].at)[0] ?? null
  const authOkUe = UE_NODES
    .filter((id) => auth[id].okAt > 0 && nowMs - auth[id].okAt < 2200)
    .sort((a, z) => auth[z].okAt - auth[a].okAt)[0] ?? null
  const authFailUe = UE_NODES
    .filter((id) => auth[id].failAt > 0 && nowMs - auth[id].failAt < 3200)
    .sort((a, z) => auth[z].failAt - auth[a].failAt)[0] ?? null

  // P8: handover beats — busy while in flight, success/reest on completion
  const hoBusyUe = UE_NODES
    .filter((id) => mobility[id].inFlight && nowMs - mobility[id].hoStartAt < 8000)
    .sort((a, z) => mobility[z].hoStartAt - mobility[a].hoStartAt)[0] ?? null
  const hoDoneUe = UE_NODES
    .filter((id) => mobility[id].hoDoneAt > 0 && nowMs - mobility[id].hoDoneAt < 2500)
    .sort((a, z) => mobility[z].hoDoneAt - mobility[a].hoDoneAt)[0] ?? null

  let caption: string
  let captionColor = '#9ca3af'
  if (confFlashFresh) {
    const who = peerLabel(ueImsi, confFlashFresh.imsi)
    caption = confFlashFresh.type === 'start' ? `${who} 发起多方通话`
      : confFlashFresh.type === 'join' ? `${who} 加入会议（${confUes.length} 方）`
        : confFlashFresh.type === 'leave' ? `${who} 离开了会议`
          : '会议结束'
    captionColor = confFlashFresh.type === 'end' ? '#9ca3af' : KIND_COLOR.conf
  } else if (authFailUe) {
    caption = `鉴权失败：${AUTH_CAUSE[auth[authFailUe].cause] ?? '未知原因'}`
    captionColor = '#ef4444'
  } else if (hoBusyUe) {
    caption = `${NODE_LABEL[hoBusyUe]} 正在切换到 ${mobility[hoBusyUe].hoTo === '2' ? 'BS2' : 'BS1'}…`
    captionColor = '#818cf8'
  } else if (hoDoneUe) {
    const m = mobility[hoDoneUe]
    caption = `${NODE_LABEL[hoDoneUe]} 已切换到 ${m.serving === '2' ? 'BS2' : 'BS1'}（${m.hoPath === 'reest' ? '重建' : '切换成功'}）`
    captionColor = '#10b981'
  } else if (authBusyUe) {
    caption = `${NODE_LABEL[authBusyUe]} 5G-AKA 鉴权中…`
    captionColor = '#f97316'
  } else if (authOkUe) {
    caption = `${NODE_LABEL[authOkUe]} 鉴权通过`
    captionColor = '#10b981'
  } else if (wakeUe && (!pagedUe || sleep[wakeUe].resumedAt >= sleep[pagedUe].pagedAt)) {
    caption = `${NODE_LABEL[wakeUe]} 唤醒恢复（RRC Resume）`
    captionColor = '#10b981'
  } else if (pagedUe) {
    caption = `寻呼 ${NODE_LABEL[pagedUe]}…`
    captionColor = '#a78bfa'
  } else if (activeConf && phase === 'in-call') {
    const names = confUes.map((id) => NODE_LABEL[id]).join('·')
    caption = `多方通话中（${confUes.length} 方）${names ? ` ${names}` : ''} ${fmtElapsed(activeConf.startedAt, nowMs)}`
    captionColor = KIND_COLOR.conf
  } else if (phase === 'dialing') {
    const c = callers[0]
    caption = `${c ? NODE_LABEL[c.ue] : 'UE'} 正在呼叫 ${c ? peerLabel(ueImsi, c.call.peer) : ''}…`
    captionColor = '#60a5fa'
  } else if (phase === 'ringing') {
    const c = callees.find(({ ue, call }) => !hasMedia(ue, call)) ?? callees[0]
    caption = `${c ? NODE_LABEL[c.ue] : 'UE'} 来电振铃中${c ? `（${peerLabel(ueImsi, c.call.peer)} 呼入）` : ''}`
    captionColor = '#f59e0b'
  } else if (phase === 'in-call') {
    const live = dialogs.filter((d) => d.live)
    const shown = live.slice(0, 2)
    if (live.length === 0) {
      // only conf stream entries visible (CONF_* slid out of the window)
      caption = '多方通话中'
      captionColor = KIND_COLOR.conf
    } else {
      caption = shown
        .map((d) => `${ueName(d.from, d.peer)} ↔ ${ueName(d.to, d.peer)} ${KIND_SHORT[d.kind]} ${fmtElapsed(d.startedAt, nowMs)}`)
        .join(' ｜ ')
      if (live.length > shown.length) caption += ` ｜ +${live.length - shown.length}`
      if (qosConcurrent) caption += '（QoS 优先级调度中）'
      captionColor = KIND_COLOR[shown[0]?.kind ?? 'voice']
    }
  } else if (phase === 'failed' && failFresh) {
    const REASON: Record<string, string> = {
      busy: '忙线中，请稍后再拨',
      declined: '对方已拒接',
      timeout: '无人接听',
      unreachable: '对方不在服务区',
      cancel: '呼叫已取消',
    }
    caption = `${NODE_LABEL[failFresh.node]} · ${REASON[failFresh.reason] ?? `呼叫失败（${failFresh.reason}）`}`
    captionColor = '#ef4444'
  } else if (phase === 'ended') {
    caption = '通话结束'
  } else if (msgFlashFresh) {
    caption = `${NODE_LABEL[msgFlashFresh.from]} 发送消息 → ${msgFlashFresh.to ? NODE_LABEL[msgFlashFresh.to] : '?'}`
    captionColor = KIND_COLOR.msg
  } else {
    caption = anyRegistered ? '待机 — 通话与消息演示就绪' : '等待 UE 入网…'
  }

  const activeStep = phase === 'dialing' ? 0 : phase === 'ringing' ? 1 : phase === 'in-call' ? 2 : phase === 'ended' || phase === 'failed' ? 3 : -1

  // keep the rAF loop alive across the whole call even if caps trim packets
  useEffect(() => {
    if (phase === 'in-call') ensureLoop()
  }, [phase, ensureLoop])

  /* ---------- render helpers ---------- */
  const now = performance.now()
  servingRef.current = {
    ue1: mobility.ue1.serving === '2' ? '2' : '1',
    ue2: mobility.ue2.serving === '2' ? '2' : '1',
    ue3: mobility.ue3.serving === '2' ? '2' : '1',
  }
  const endpoint = (p: Packet) => {
    const ue = LINK_UE[p.path]
    const bs = p.bsAt ?? LINK_BS[p.path][servingRef.current[p.path]] ?? LINK_BS[p.path]['1']
    const t = Math.min(1, Math.max(0, (now - p.t0) / p.dur))
    const k = p.dir === 'ul' ? t : 1 - t
    return {
      x: ue.x + (bs.x - ue.x) * k,
      y: ue.y + (bs.y - ue.y) * k,
      opacity: t > 0.85 ? Math.max(0, 1 - (t - 0.85) / 0.15) : 1,
    }
  }

  const phone = (id: UeId) => {
    const c = UE_C[id]
    const ueCalls = Object.values(calls[id] ?? {}) as CallState[]
    const nas = ueNas[id]
    const registered = nas === 'REGISTERED'
    const statusColor = registered ? '#10b981' : presence[id] === 'RUNNING' ? '#f59e0b' : '#6b7280'
    const ringing = ueCalls.some((call) => !hasMedia(id, call) && call.role === 'callee')
    const calling = ueCalls.some((call) => !hasMedia(id, call) && call.role === 'caller')
    const ringback = calling && b.ringbackAt[id] > 0
    const failFlash = !!b.fail && nowMs - b.fail.at < 3000 && b.fail.node === id
    const attachFresh = registered && nowMs - attachAtRef.current[id] < 2500
    // conference scene: member UEs render 多方 state in the conf color
    const myConf = activeConf && confUes.includes(id) ? activeConf : null
    const isConfHost = myConf != null && ueImsi[id] != null && myConf.host === ueImsi[id]
    const hasConfStream = ueCalls.some((call) => call.kind === 'conf')
    // 发起会议 candidate: attached, idle, no conference running, 2 attached peers
    const confPeers = UE_NODES.filter((p) => p !== id && ueImsi[p])
    const canStartConf = registered && !myConf && !activeConf && ueCalls.length === 0 && confPeers.length >= 2
    // P6 lock-screen: suspended UE dims; wake/paging get their own pulses
    const sleeping = sleep[id].inactive
    const waking = wakeUe === id
    const paged = pagedUe === id
    const canSleep = registered && !sleeping && !myConf && ueCalls.length === 0
    const top = c.y - PHONE_H / 2
    const kindText = (k: ServiceKind) => KIND_SHORT[k]
    const screenText = sleeping ? '锁屏'
      : ringing ? '来电'
      : myConf ? `多方通话(${confUes.length})`
        : calling ? (ringback ? '对方振铃' : '呼叫中')
          : authBusyUe === id ? '鉴权中'
            : ueCalls.length > 0 ? ueCalls.map((call) => kindText(call.kind)).join('+')
              : '待机'
    const screenColor = sleeping ? '#6b7280'
      : ringing ? '#f59e0b'
      : myConf ? KIND_COLOR.conf
        : calling ? '#f59e0b'
          : authBusyUe === id ? '#f97316'
            : ueCalls.length > 0 ? KIND_COLOR[ueCalls[0].kind] : '#6b7280'
    const screenSize = screenText.length > 2 ? 12 : 15
    return (
      <g onClick={() => setInspect(id)} style={{ cursor: 'pointer' }}>
        {/* ring waves */}
        {ringing && [0, 1, 2].map((i) => (
          <circle key={i} cx={c.x} cy={c.y} r={48} fill="none" stroke="#f59e0b" strokeWidth={1.5} opacity={0}>
            <animate attributeName="r" values="48;82" dur="1.5s" begin={`${i * 0.5}s`} repeatCount="indefinite" />
            <animate attributeName="opacity" values="0.7;0" dur="1.5s" begin={`${i * 0.5}s`} repeatCount="indefinite" />
          </circle>
        ))}
        {/* paging waves: BS is calling the dimmed phone (violet) */}
        {paged && [0, 1, 2].map((i) => (
          <circle key={i} cx={c.x} cy={c.y} r={48} fill="none" stroke="#a78bfa" strokeWidth={1.5} opacity={0}>
            <animate attributeName="r" values="48;82" dur="1.5s" begin={`${i * 0.5}s`} repeatCount="indefinite" />
            <animate attributeName="opacity" values="0.7;0" dur="1.5s" begin={`${i * 0.5}s`} repeatCount="indefinite" />
          </circle>
        ))}
        {/* wake pulse: fast green flash, NOT the long attach pulse */}
        {waking && (
          <circle cx={c.x} cy={c.y} r={46} fill="none" stroke="#10b981" strokeWidth={2.5}>
            <animate attributeName="r" values="30;62" dur="0.9s" repeatCount="indefinite" />
            <animate attributeName="opacity" values="0.9;0" dur="0.9s" repeatCount="indefinite" />
          </circle>
        )}
        {/* in-call pulse: one ring per concurrent call kind */}
        {!ringing && ueCalls.map((call) => (
          <circle key={call.kind} cx={c.x} cy={c.y} r={52} fill="none" stroke={KIND_COLOR[call.kind]} strokeWidth={2}>
            <animate attributeName="r" values="50;58;50" dur="1.6s" repeatCount="indefinite" />
            <animate attributeName="stroke-opacity" values="0.9;0.15;0.9" dur="1.6s" repeatCount="indefinite" />
          </circle>
        ))}
        {/* conf member pulse before the first conf stats arrive */}
        {!ringing && myConf && !hasConfStream && (
          <circle cx={c.x} cy={c.y} r={52} fill="none" stroke={KIND_COLOR.conf} strokeWidth={2}>
            <animate attributeName="r" values="50;58;50" dur="1.6s" repeatCount="indefinite" />
            <animate attributeName="stroke-opacity" values="0.9;0.15;0.9" dur="1.6s" repeatCount="indefinite" />
          </circle>
        )}
        {/* attach pulse on the link */}
        {attachFresh && (
          <circle cx={LINK_UE[id].x} cy={LINK_UE[id].y} r={8} fill="none" stroke="#10b981" strokeWidth={2.5}>
            <animate attributeName="r" values="8;46" dur="1.1s" repeatCount="indefinite" />
            <animate attributeName="opacity" values="0.9;0" dur="1.1s" repeatCount="indefinite" />
          </circle>
        )}
        {/* dimmed while RRC_INACTIVE: body, labels, bars all fade together */}
        <g opacity={sleeping ? 0.45 : 1}>
        <g>
          {ringing && (
            <animateTransform
              attributeName="transform" type="rotate"
              values={`-2.5 ${c.x} ${c.y};2.5 ${c.x} ${c.y};-2.5 ${c.x} ${c.y}`}
              dur="0.24s" repeatCount="indefinite"
            />
          )}
          <rect x={c.x - PHONE_W / 2} y={top} width={PHONE_W} height={PHONE_H} rx={14}
            fill="#0a0f18" stroke={statusColor} strokeWidth={2.5} />
          <rect x={c.x - PHONE_W / 2 + 8} y={top + 20} width={PHONE_W - 16} height={PHONE_H - 46} rx={6}
            fill="rgba(255,255,255,0.03)" stroke="rgba(255,255,255,0.08)" />
          <line x1={c.x - 10} y1={top + 11} x2={c.x + 10} y2={top + 11} stroke="#374151" strokeWidth={2.5} strokeLinecap="round" />
          <circle cx={c.x} cy={top + PHONE_H - 12} r={4} fill="none" stroke="#374151" strokeWidth={1.5} />
          <text x={c.x} y={c.y + 5} textAnchor="middle" fill={screenColor} fontSize={screenSize} fontWeight={800}>
            {screenText}
          </text>
        </g>
        <text x={c.x} y={top - 14} textAnchor="middle" fill="#e5e7eb" fontSize={14} fontWeight={800}>{NODE_LABEL[id]}</text>
        <text x={c.x} y={c.y + PHONE_H / 2 + 18} textAnchor="middle" fill={sleeping ? '#f59e0b' : statusColor} fontSize={10.5} fontWeight={700}>
          {sleeping ? '休眠中' : registered ? '已入网' : presence[id] === 'RUNNING' ? nas : '离线'}
        </text>
        <text x={c.x} y={c.y + PHONE_H / 2 + 33} textAnchor="middle" fill="#6b7280" fontSize={10} fontFamily="monospace">
          {ueImsi[id] ? shortImsi(ueImsi[id]!) : '—'}
        </text>
        {/* signal bars (left) + DL MCS badge (right) on the status line */}
        {radio[id].sinr != null && (
          <SignalBars
            x={c.x - PHONE_W / 2 - 5 - SIGNAL_BARS_WIDTH}
            y={c.y + PHONE_H / 2 + 22}
            bars={sinrBars(radio[id].sinr)}
            color={qualityTier(radio[id].sinr) === 'good' ? '#34d399' : qualityTier(radio[id].sinr) === 'mid' ? '#fbbf24' : '#f87171'}
          />
        )}
        {mcsLabel(radio[id].mcs) && (
          <g>
            <rect x={c.x + PHONE_W / 2 + 5} y={c.y + PHONE_H / 2 + 10} width={36} height={13} rx={3.5}
              fill={`${mcsColor(radio[id].mcs)}1a`} stroke={mcsColor(radio[id].mcs)} strokeWidth={0.9} />
            <text x={c.x + PHONE_W / 2 + 23} y={c.y + PHONE_H / 2 + 20} textAnchor="middle"
              fill={mcsColor(radio[id].mcs)} fontSize={8} fontWeight={800}>
              {mcsLabel(radio[id].mcs)}
            </text>
          </g>
        )}
        </g>

        {/* ringing callee: 接听 / 拒接 */}
        {ringing && (
          <g onClick={(e) => { e.stopPropagation(); onCommand(id, 'answer') }} style={{ cursor: 'pointer' }}>
            <rect x={c.x + PHONE_W / 2 + 8} y={c.y - 22} width={42} height={18} rx={5} fill="rgba(16,185,129,0.15)" stroke="#10b981" strokeWidth={1.2} />
            <text x={c.x + PHONE_W / 2 + 29} y={c.y - 9} textAnchor="middle" fill="#10b981" fontSize={11} fontWeight={800}>接听</text>
          </g>
        )}
        {ringing && (
          <g onClick={(e) => { e.stopPropagation(); onCommand(id, 'decline') }} style={{ cursor: 'pointer' }}>
            <rect x={c.x + PHONE_W / 2 + 8} y={c.y + 4} width={42} height={18} rx={5} fill="rgba(239,68,68,0.12)" stroke="#ef4444" strokeWidth={1.2} />
            <text x={c.x + PHONE_W / 2 + 29} y={c.y + 17} textAnchor="middle" fill="#ef4444" fontSize={11} fontWeight={800}>拒接</text>
          </g>
        )}
        {/* ringing/dialing caller: 取消 (CANCEL via call end) — hidden while
            an incoming ring or a conference owns the side buttons */}
        {calling && !ringing && !myConf && (
          <g onClick={(e) => { e.stopPropagation(); onCommand(id, 'call end') }} style={{ cursor: 'pointer' }}>
            <rect x={c.x + PHONE_W / 2 + 8} y={c.y - 9} width={46} height={18} rx={5} fill="rgba(239,68,68,0.12)" stroke="#ef4444" strokeWidth={1.2} />
            <text x={c.x + PHONE_W / 2 + 31} y={c.y + 4} textAnchor="middle" fill="#ef4444" fontSize={11} fontWeight={800}>取消</text>
          </g>
        )}
        {/* idle attached UE: 发起会议 (conf <peerA> <peerB>, other two UEs) */}
        {canStartConf && (
          <g onClick={(e) => {
            e.stopPropagation()
            onCommand(id, `conf ${ueImsi[confPeers[0]]} ${ueImsi[confPeers[1]]}`)
          }} style={{ cursor: 'pointer' }}>
            <rect x={c.x + PHONE_W / 2 + 8} y={c.y - 9} width={46} height={18} rx={5} fill="rgba(45,212,191,0.12)" stroke={KIND_COLOR.conf} strokeWidth={1.2} />
            <text x={c.x + PHONE_W / 2 + 31} y={c.y + 4} textAnchor="middle" fill={KIND_COLOR.conf} fontSize={10} fontWeight={800}>发起会议</text>
          </g>
        )}
        {/* connected & idle: 锁屏 (sleep -> RRC_INACTIVE), stacked under 发起会议 */}
        {canSleep && (
          <g onClick={(e) => { e.stopPropagation(); onCommand(id, 'sleep') }} style={{ cursor: 'pointer' }}>
            <rect x={c.x + PHONE_W / 2 + 8} y={c.y + 13} width={46} height={18} rx={5} fill="rgba(148,163,184,0.10)" stroke="#64748b" strokeWidth={1.2} />
            <text x={c.x + PHONE_W / 2 + 31} y={c.y + 26} textAnchor="middle" fill="#94a3b8" fontSize={10} fontWeight={800}>锁屏</text>
          </g>
        )}
        {/* suspended: 唤醒 (wake -> fast resume) */}
        {sleeping && (
          <g onClick={(e) => { e.stopPropagation(); onCommand(id, 'wake') }} style={{ cursor: 'pointer' }}>
            <rect x={c.x + PHONE_W / 2 + 8} y={c.y - 9} width={46} height={18} rx={5} fill="rgba(16,185,129,0.15)" stroke="#10b981" strokeWidth={1.2} />
            <text x={c.x + PHONE_W / 2 + 31} y={c.y + 4} textAnchor="middle" fill="#10b981" fontSize={10} fontWeight={800}>唤醒</text>
          </g>
        )}
        {/* conference host: 结束会议 (conf end); other members: 离开 (call end) */}
        {myConf && (
          <g onClick={(e) => { e.stopPropagation(); onCommand(id, isConfHost ? 'conf end' : 'call end') }} style={{ cursor: 'pointer' }}>
            <rect x={c.x + PHONE_W / 2 + 8} y={c.y - 9} width={46} height={18} rx={5} fill="rgba(239,68,68,0.12)" stroke="#ef4444" strokeWidth={1.2} />
            <text x={c.x + PHONE_W / 2 + 31} y={c.y + 4} textAnchor="middle" fill="#ef4444" fontSize={10} fontWeight={800}>
              {isConfHost ? '结束会议' : '离开'}
            </text>
          </g>
        )}
        {/* failed attempt: busy-tone flash on the phone */}
        {failFlash && (
          <circle cx={c.x} cy={c.y} r={52} fill="none" stroke="#ef4444" strokeWidth={2.5}>
            <animate attributeName="stroke-opacity" values="0.9;0.1;0.9" dur="0.32s" repeatCount="indefinite" />
          </circle>
        )}
        {/* auth failure: red flash on the phone */}
        {authFailUe === id && (
          <circle cx={c.x} cy={c.y} r={52} fill="none" stroke="#ef4444" strokeWidth={2.5}>
            <animate attributeName="stroke-opacity" values="0.9;0.1;0.9" dur="0.4s" repeatCount="indefinite" />
          </circle>
        )}
      </g>
    )
  }

  const tower = (cell: '1' | '2') => {
    const ant = BS_ANT[cell]
    const color = cell === '1' ? '#60a5fa' : '#818cf8'
    const nodeId: NodeId = cell === '1' ? 'bs' : 'bs2'
    const serving = UE_NODES.filter((id) => (mobility[id].serving === '2' ? '2' : '1') === cell).length
    return (
      <g key={cell} onClick={() => setInspect(nodeId)} style={{ cursor: 'pointer' }}>
        {[16, 27, 38].map((r, i) => (
          <circle key={r} cx={ant.x} cy={ant.y} r={r} fill="none" stroke={color} strokeWidth={1.2} opacity={0.25}>
            <animate attributeName="opacity" values="0.4;0.08;0.4" dur="2.4s" begin={`${i * 0.4}s`} repeatCount="indefinite" />
          </circle>
        ))}
        <circle cx={ant.x} cy={ant.y} r={5.5} fill={color} />
        <line x1={ant.x} y1={ant.y + 5} x2={ant.x - 42} y2={278} stroke="#4b5563" strokeWidth={3} />
        <line x1={ant.x} y1={ant.y + 5} x2={ant.x + 42} y2={278} stroke="#4b5563" strokeWidth={3} />
        <line x1={ant.x - 15} y1={180} x2={ant.x + 15} y2={180} stroke="#4b5563" strokeWidth={2} />
        <line x1={ant.x - 27} y1={230} x2={ant.x + 27} y2={230} stroke="#4b5563" strokeWidth={2} />
        <rect x={ant.x - 50} y={278} width={100} height={9} rx={3} fill="#1f2937" stroke="#4b5563" />
        <text x={ant.x + 58} y={266} fill="#e5e7eb" fontSize={13.5} fontWeight={800}>{cell === '1' ? 'gNB 基站' : 'gNB2 基站'}</text>
        <text x={ant.x + 58} y={283} fill="#10b981" fontSize={11} fontWeight={600}>服务 {serving} UE</text>
      </g>
    )
  }

  const bubblesFor = (id: UeId) => chat.filter((c) => c.from === id || c.to === id).slice(-4)

  const inspectEvents = inspect
    ? messages.filter((m) => nodeOf(m) === inspect && !INSPECT_SKIP.has(m.event)).slice(-6).reverse()
    : []

  /* bubble columns follow the scene layout: UE1 left, UE3 center, UE2 right */
  const bubbleCols: { id: UeId; align: 'flex-start' | 'center' | 'flex-end' }[] = [
    { id: 'ue1', align: 'flex-start' },
    { id: 'ue3', align: 'center' },
    { id: 'ue2', align: 'flex-end' },
  ]

  return (
    <div style={{ position: 'relative', display: 'flex', flexDirection: 'column', height: '100%', overflow: 'hidden' }}>
      <svg viewBox="0 0 960 500" style={{ width: '100%', flexGrow: 1, display: 'block' }} preserveAspectRatio="xMidYMid meet">
        {/* links — each UE connects to its SERVING tower; width hints the
            SINR tier; a suspended UE's link goes dashed/dim; during a
            handover the old link fades dashed while the new one marches in */}
        {UE_NODES.map((id) => {
          const active = ueNas[id] === 'REGISTERED'
          const sleeping = sleep[id].inactive
          const tier = qualityTier(radio[id].sinr)
          const w = tier === 'good' ? 2.3 : tier === 'poor' ? 1.2 : 1.8
          const mob = mobility[id]
          const serving = mob.serving === '2' ? '2' : '1'
          const bs = LINK_BS[id][serving]
          const mx = (LINK_UE[id].x + bs.x) / 2
          const my = (LINK_UE[id].y + bs.y) / 2
          const hoTarget = mob.inFlight && mob.hoTo ? LINK_BS[id][mob.hoTo === '2' ? '2' : '1'] : null
          return (
            <g key={id}>
              <line x1={LINK_UE[id].x} y1={LINK_UE[id].y} x2={bs.x} y2={bs.y}
                stroke={sleeping ? '#475569' : active ? '#10b981' : '#374151'} strokeWidth={w}
                strokeDasharray={sleeping || !active || mob.inFlight ? '6 6' : 'none'}
                opacity={sleeping ? 0.35 : mob.inFlight ? 0.4 : 0.65}>
                {active && !sleeping && !mob.inFlight && <animate attributeName="stroke-opacity" values="0.75;0.3;0.75" dur="2.4s" repeatCount="indefinite" />}
              </line>
              {hoTarget && (
                <line x1={LINK_UE[id].x} y1={LINK_UE[id].y} x2={hoTarget.x} y2={hoTarget.y}
                  stroke={mob.hoTo === '2' ? '#818cf8' : '#60a5fa'} strokeWidth={2.2}
                  strokeDasharray="9 6" opacity={0.95}>
                  <animate attributeName="stroke-dashoffset" values="15;0" dur="0.5s" repeatCount="indefinite" />
                </line>
              )}
              {radio[id].sinr != null && (
                <text x={mx + 8} y={my - 3} fill="#4b5563" fontSize={8} fontFamily="monospace">
                  {radio[id].sinr}dB
                </text>
              )}
            </g>
          )
        })}

        {/* QoS bearer pips at the UE end of each link (sig/voice/video/BE) */}
        {UE_NODES.map((id) => {
          const list = bearers[id] ?? []
          const { x, y } = LINK_UE[id]
          return list.map((cls, i) => (
            <circle key={cls} cx={x + (i - (list.length - 1) / 2) * 9} cy={y - 9} r={3}
              fill={BEARER_COLOR[cls]} stroke="#05070a" strokeWidth={0.9}>
              <title>{cls === 'sig' ? 'QCI5 信令' : cls === 'voice' ? 'QCI1 语音' : cls === 'video' ? 'QCI2 视频' : 'QCI9 尽力而为'}</title>
            </circle>
          ))
        })}

        {/* packets */}
        {packetsRef.current.map((p) => {
          const e = endpoint(p)
          if (p.glyph === 'lock') {
            // AKA challenge/result: padlock glyph
            return (
              <g key={p.id} opacity={e.opacity}>
                <rect x={e.x - 4} y={e.y - 1.5} width={8} height={6.5} rx={1.5} fill={p.color} />
                <path d={`M ${e.x - 2.6} ${e.y - 1.5} v -1.6 a 2.6 2.6 0 0 1 5.2 0 v 1.6`} fill="none" stroke={p.color} strokeWidth={1.5} />
              </g>
            )
          }
          if (p.glyph === 'key') {
            // AKA response: key glyph
            return (
              <g key={p.id} opacity={e.opacity}>
                <circle cx={e.x - 2.4} cy={e.y} r={2.5} fill="none" stroke={p.color} strokeWidth={1.7} />
                <line x1={e.x} y1={e.y} x2={e.x + 5.5} y2={e.y} stroke={p.color} strokeWidth={1.7} />
                <line x1={e.x + 3.4} y1={e.y} x2={e.x + 3.4} y2={e.y + 2.4} stroke={p.color} strokeWidth={1.3} />
                <line x1={e.x + 5.5} y1={e.y} x2={e.x + 5.5} y2={e.y + 2.4} stroke={p.color} strokeWidth={1.3} />
              </g>
            )
          }
          if (p.kind === 'msg') {
            return (
              <g key={p.id} opacity={e.opacity}>
                <rect x={e.x - 6} y={e.y - 4.5} width={12} height={9} rx={2.5} fill={p.color} />
                <polygon points={`${e.x - 2.5},${e.y + 4.5} ${e.x + 1.5},${e.y + 4.5} ${e.x - 2.5},${e.y + 8}`} fill={p.color} />
              </g>
            )
          }
          if (p.kind === 'video') {
            return <circle key={p.id} cx={e.x} cy={e.y} r={p.r} fill="none" stroke={p.color} strokeWidth={2.4} opacity={e.opacity} />
          }
          return <circle key={p.id} cx={e.x} cy={e.y} r={p.r} fill={p.color} opacity={e.opacity} />
        })}

        {tower('1')}
        {tower('2')}
        {UE_NODES.map((id) => (
          <React.Fragment key={id}>{phone(id)}</React.Fragment>
        ))}
      </svg>

      {/* per-phone chat bubbles */}
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: 16, padding: '4px 28px 8px', minHeight: 76 }}>
        {bubbleCols.map(({ id, align }) => (
          <div key={id} style={{ width: 260, display: 'flex', flexDirection: 'column', gap: 4, alignItems: align }}>
            {bubblesFor(id).map((c) => {
              const outgoing = c.from === id
              const color = outgoing ? NODE_COLOR[id] : '#9ca3af'
              return (
                <div key={c.id} style={{
                  maxWidth: '100%', background: `${color}14`, border: `1px solid ${color}3d`,
                  borderRadius: 8, padding: '3px 9px', fontSize: 11.5, color: '#e5e7eb',
                }}>
                  {c.text}
                  <span style={{ marginLeft: 6, fontSize: 9.5, color: '#4b5563', fontFamily: 'monospace' }}>{c.time}</span>
                </div>
              )
            })}
          </div>
        ))}
      </div>

      {/* caption bar + call steps */}
      <div style={{
        flexShrink: 0, display: 'flex', alignItems: 'center', gap: 18,
        padding: '10px 20px', borderTop: '1px solid var(--border-color)', background: 'rgba(255,255,255,0.015)',
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, flexShrink: 0 }}>
          {STEPS.map((s, i) => {
            const on = i === activeStep
            const done = activeStep > i
            return (
              <React.Fragment key={s}>
                {i > 0 && <span style={{ width: 14, height: 1, background: '#374151' }} />}
                <span style={{
                  width: 8, height: 8, borderRadius: '50%', flexShrink: 0,
                  background: on ? captionColor : done ? '#6b7280' : 'rgba(255,255,255,0.08)',
                  boxShadow: on ? `0 0 8px ${captionColor}` : 'none',
                }} />
                <span style={{ fontSize: 11, fontWeight: on ? 800 : 500, color: on ? '#e5e7eb' : '#6b7280' }}>{s}</span>
              </React.Fragment>
            )
          })}
        </div>
        <div style={{ flexGrow: 1, textAlign: 'center', fontSize: 16, fontWeight: 800, color: captionColor, letterSpacing: 0.5 }}>
          {caption}
        </div>
        <div style={{
          flexShrink: 0, width: 300, display: 'flex', flexWrap: 'wrap', justifyContent: 'flex-end',
          rowGap: 2, fontSize: 11, fontFamily: 'monospace', color: 'var(--text-secondary)',
        }}>
          {phase === 'in-call' && allCalls.some(({ call }) => call.stats) && (
            <>
              {allCalls.filter(({ call }) => call.stats).map(({ ue, call }) => {
                const qci = call.stats!.qci ? QCI_INFO[call.stats!.qci] : undefined
                return (
                  <span key={`${ue}:${call.kind}`} style={{ marginLeft: 12, whiteSpace: 'nowrap' }}>
                    {qci && <span style={{ color: qci.color, fontWeight: 800 }}>{qci.label} </span>}
                    <span style={{ color: KIND_COLOR[call.kind], fontWeight: 700 }}>
                      {NODE_LABEL[ue]}{call.kind !== 'voice' ? `·${KIND_SHORT[call.kind]}` : ''}
                    </span>
                    {' '}丢包 {call.stats!.loss} · RTT {call.stats!.rtt}ms
                  </span>
                )
              })}
            </>
          )}
        </div>
      </div>

      {/* node inspector popover */}
      {inspect && (
        <>
          <div style={{ position: 'absolute', inset: 0, zIndex: 5 }} onClick={() => setInspect(null)} />
          <div className="glass-panel" style={{
            position: 'absolute', top: 14, right: 14, width: 300, zIndex: 6,
            padding: 14, background: '#0a0f18', border: '1px solid var(--border-color)',
          }} onClick={(e) => e.stopPropagation()}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 10 }}>
              <span style={{ fontSize: 13, fontWeight: 800, color: NODE_COLOR[inspect] }}>{NODE_LABEL[inspect]} 最近事件</span>
              <button onClick={() => setInspect(null)} style={{ background: 'none', border: 'none', color: '#6b7280', cursor: 'pointer', fontSize: 14 }}>✕</button>
            </div>
            {inspectEvents.length === 0 ? (
              <div style={{ fontSize: 12, color: '#4b5563', fontStyle: 'italic' }}>暂无事件</div>
            ) : (
              inspectEvents.map((m, i) => {
                const time = m.timestamp ? m.timestamp.split('T')[1]?.replace('Z', '') : ''
                const fields = Object.entries(m.fields || {}).filter(([k]) => k !== 'hex').slice(0, 3)
                return (
                  <div key={m._seq ?? i} style={{ padding: '4px 0', borderBottom: '1px solid rgba(255,255,255,0.03)', fontSize: 11.5 }}>
                    <span style={{ color: '#4b5563', fontFamily: 'monospace', marginRight: 6 }}>{time}</span>
                    <span style={{ color: '#e5e7eb', fontWeight: 700 }}>{m.event}</span>
                    {fields.length > 0 && (
                      <span style={{ color: '#6b7280', fontFamily: 'monospace' }}> {fields.map(([k, v]) => `${k}=${v}`).join(' ')}</span>
                    )}
                  </div>
                )
              })
            )}
            {(inspect === 'ue1' || inspect === 'ue2' || inspect === 'ue3') && (
              <label style={{ display: 'flex', alignItems: 'center', gap: 6, marginTop: 10, fontSize: 12, color: '#9ca3af', cursor: 'pointer' }}>
                <input
                  type="checkbox"
                  checked={autoAns[inspect]}
                  onChange={(e) => {
                    const v = e.target.checked
                    setAutoAns((p) => ({ ...p, [inspect]: v }))
                    onCommand(inspect, v ? 'autoanswer 4000' : 'autoanswer off')
                  }}
                />
                自动接听（4 秒）
              </label>
            )}
            <button
              onClick={() => { onInspectNode(inspect); setInspect(null) }}
              style={{
                marginTop: 10, width: '100%', padding: '6px 0', fontSize: 12, fontWeight: 700,
                background: 'rgba(59,130,246,0.1)', color: '#60a5fa', border: '1px solid rgba(59,130,246,0.3)',
                borderRadius: 6, cursor: 'pointer',
              }}
            >
              查看原始日志 →
            </button>
          </div>
        </>
      )}
    </div>
  )
}

export default StoryView

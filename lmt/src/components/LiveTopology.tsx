import React, { useCallback, useEffect, useRef, useState } from 'react'
import ev, { MSC_UPLINK, MSC_DOWNLINK } from '../events'
import { LogEvent } from '../hooks/useWebSocket'
import { NodeId, nodeOf, NODE_LABEL, UE_NODES, isBs } from '../nodes'
import { PduEntry, pduFromEvent } from './PduDetail'
import { svc, kindOf, imsiToUe, shortImsi, KIND_COLOR, KIND_SHORT, BEARER_COLOR, BearerClass, CallMap, ConfState, MobilityInfo, RadioInfo, ServiceKind, SleepInfo, UeId, confUeIds, sinrBars, qualityTier, mcsLabel, mcsColor } from '../services'
import SignalBars, { SIGNAL_BARS_WIDTH } from './SignalBars'

interface LiveTopologyProps {
  messages: LogEvent[]
  presence: Record<NodeId, string>
  ueNas: Record<UeId, string>
  ueCrnti: Record<UeId, string>
  /** IMSI per UE (the "phone number"); null until attach is seen. */
  ueImsi: Record<UeId, string | null>
  /** Active calls per UE, keyed by service kind (concurrent voice+video+conf). */
  calls: CallMap
  /** Active QoS bearer classes per UE (link pips). */
  bearers: Record<UeId, BearerClass[]>
  /** Conferences keyed by conf_id (P4 3-party calls). */
  confs: ConfState[]
  /** Latest radio telemetry per UE (SINR/MCS/TX power); nulls when unseen. */
  radio: Record<UeId, RadioInfo>
  /** RRC Inactive / paging state per UE (P6); all-false on older backends. */
  sleep: Record<UeId, SleepInfo>
  /** Serving cell + handover state per UE (P8); all cell-1 on single-BS streams. */
  mobility: Record<UeId, MobilityInfo>
  bsRegistered: string
  /** Registered UEs on the second gNB ('' when no bs2 exists). */
  bs2Registered: string
  selected: NodeId | null
  onSelect: (n: NodeId) => void
  onCommand: (target: NodeId, cmd: string) => void
  onOpenPdu: (p: PduEntry) => void
}

/* --- scene geometry (viewBox 420x368): two gNBs top, UEs in a triangle --- */
const BS1 = { x: 120, y: 58, w: 70, h: 56 }
const BS2 = { x: 300, y: 58, w: 70, h: 56 }
/** Serving-tower center per cell id. */
const BS_POS: Record<string, { x: number; y: number }> = { '1': { x: BS1.x, y: BS1.y }, '2': { x: BS2.x, y: BS2.y } }
const UE_POS: Record<UeId, { x: number; y: number }> = {
  ue1: { x: 64, y: 218 },
  ue2: { x: 356, y: 218 },
  ue3: { x: 210, y: 308 },
}
const UE_R = 30

type Path = UeId
type Dir = 'ul' | 'dl'

interface Packet {
  id: number
  path: Path
  dir: Dir
  color: string
  r: number
  kind: ServiceKind | null
  t0: number
  dur: number
}

interface Chip {
  id: number
  label: string
  color: string
  pdu: PduEntry | null
}

/** First hop in flight, waiting for its APP_FORWARD to launch the second hop. */
interface PendingHop {
  src: string
  dst: string
  kind: ServiceKind
  /** performance.now() timestamp at which the UL dot reaches the BS. */
  landAt: number
}

const MAX_PACKETS = 24
const MAX_PER_PATH = 10
const MIN_SPAWN_MS = 90 // per-path throttle: PDU floods get sampled, not queued
const FLIGHT_MS = 900

/** Per-kind hop timing: voice darts, video cruises, msg pulses along slower. */
const KIND_DUR: Record<ServiceKind, number> = { voice: 620, video: 900, msg: 1050, conf: 620 }
const KIND_R: Record<ServiceKind, number> = { voice: 3, video: 5, msg: 4, conf: 3 }

const LAYER_COLOR: Record<string, string> = {
  MAC: '#3b82f6', RLC: '#8b5cf6', PDCP: '#10b981', RRC: '#f59e0b', NAS: '#ef4444', APP: '#6b7280',
}

function categoryColor(m: LogEvent): string {
  const e = m.event
  if (e.startsWith('MAC_RACH') || e.startsWith('RACH') || e.startsWith('RA_')) return '#f59e0b'
  if (e.startsWith('RRC')) return '#3b82f6'
  if (e.startsWith('NAS') || e.startsWith('SEC')) return '#ef4444'
  if (e.startsWith('APP') || e.startsWith('TRAFFIC')) return '#10b981'
  return '#9ca3af'
}

function packetSpec(m: LogEvent, bsPath: (m: LogEvent) => Path): { path: Path; dir: Dir; color: string; r: number } | null {
  const node = nodeOf(m)
  if (!node) return null

  let dir: Dir | null = null
  if (MSC_UPLINK[m.event as keyof typeof MSC_UPLINK]) dir = 'ul'
  else if (MSC_DOWNLINK[m.event as keyof typeof MSC_DOWNLINK]) dir = 'dl'
  else if (m.event === ev.PDU_TRACE) {
    const d = (m.fields.direction || '').toUpperCase()
    if (isBs(node)) dir = d === 'TX' ? 'dl' : d === 'RX' ? 'ul' : null
    else dir = d === 'TX' ? 'ul' : d === 'RX' ? 'dl' : null
  } else if (m.event.endsWith('_TX')) dir = isBs(node) ? 'dl' : 'ul'
  else if (m.event.endsWith('_RX')) dir = isBs(node) ? 'ul' : 'dl'
  if (!dir) return null

  const path: Path = isBs(node) ? bsPath(m) : node
  let color = categoryColor(m)
  let r = 3.5
  if (m.event === ev.PDU_TRACE) {
    color = LAYER_COLOR[(m.fields.layer || '').toUpperCase()] || color
    const len = parseInt(m.fields.len || '0', 10)
    if (len > 0) r = Math.min(6, Math.max(3, 2.5 + len / 24))
  } else if (m.event.startsWith('MAC_RACH') || m.event.startsWith('RACH')) {
    r = 4
  }
  return { path, dir, color, r }
}

const stateColor = (s: string) =>
  s === 'REGISTERED' || s === 'RUNNING' ? '#10b981' : s === 'OFFLINE' ? '#6b7280' : '#f59e0b'

const cmdBtn: React.CSSProperties = {
  padding: '2px 8px',
  fontSize: 10,
  fontWeight: 700,
  fontFamily: 'monospace',
  borderRadius: 5,
  border: '1px solid rgba(255,255,255,0.12)',
  background: 'rgba(255,255,255,0.03)',
  color: '#9ca3af',
  cursor: 'pointer',
}

const msgInputStyle: React.CSSProperties = {
  background: '#111827',
  color: '#f3f4f6',
  border: '1px solid rgba(255,255,255,0.12)',
  borderRadius: 5,
  padding: '2px 6px',
  fontSize: 10,
  fontFamily: 'monospace',
  width: 76,
}

export const LiveTopology: React.FC<LiveTopologyProps> = ({
  messages, presence, ueNas, ueCrnti, ueImsi, calls, bearers, confs, radio, sleep, mobility, bsRegistered, bs2Registered, selected, onSelect, onCommand, onOpenPdu,
}) => {
  const packetsRef = useRef<Packet[]>([])
  const pendingRef = useRef<PendingHop[]>([])
  const lastSpawnRef = useRef<Record<Path, number>>({ ue1: 0, ue2: 0, ue3: 0 })
  const lastSeqRef = useRef(0)
  const primedRef = useRef(false)
  const lastActivePathRef = useRef<Path>('ue1')
  const idRef = useRef(0)
  const rafRef = useRef<number | null>(null)
  const [, setTick] = useState(0)
  const [chips, setChips] = useState<Chip[]>([])
  const [msgDraft, setMsgDraft] = useState<Record<UeId, string>>({ ue1: '', ue2: '', ue3: '' })
  /** Dial target per UE (any of the other two phones). */
  const [peerSel, setPeerSel] = useState<Record<UeId, UeId>>({ ue1: 'ue2', ue2: 'ue1', ue3: 'ue1' })

  /* BS-side events rarely name the UE: route via c_rnti when known, else the
     most recently active link so downlink dots appear on a believable path. */
  const bsPath = useCallback((m: LogEvent): Path => {
    const cr = m.fields.c_rnti
    if (cr) {
      for (const id of UE_NODES) {
        if (cr === ueCrnti[id]) return id
      }
    }
    return lastActivePathRef.current
  }, [ueCrnti])

  const loop = useCallback(() => {
    const now = performance.now()
    packetsRef.current = packetsRef.current.filter((p) => now - p.t0 < p.dur)
    setTick((t) => t + 1)
    if (packetsRef.current.length > 0) {
      rafRef.current = requestAnimationFrame(loop)
    } else {
      rafRef.current = null
    }
  }, [])

  const ensureLoop = useCallback(() => {
    if (rafRef.current == null) rafRef.current = requestAnimationFrame(loop)
  }, [loop])

  useEffect(() => () => {
    if (rafRef.current != null) cancelAnimationFrame(rafRef.current)
  }, [])

  /* Turn newly arrived events into packet dots (throttled / sampled). */
  useEffect(() => {
    if (messages.length === 0) {
      lastSeqRef.current = 0
      primedRef.current = false
      return
    }
    const now = performance.now()
    // expire unmatched first hops (their APP_FORWARD never came or came late)
    pendingRef.current = pendingRef.current.filter((p) => p.landAt > now - 5000)

    const spawn = (path: Path, dir: Dir, color: string, r: number, kind: ServiceKind | null, dur: number, t0: number, label: string, pdu: PduEntry | null) => {
      const perPath = packetsRef.current.filter((p) => p.path === path).length
      if (packetsRef.current.length >= MAX_PACKETS || perPath >= MAX_PER_PATH) return
      if (now - lastSpawnRef.current[path] < MIN_SPAWN_MS) return
      lastSpawnRef.current[path] = now
      const id = ++idRef.current
      packetsRef.current.push({ id, path, dir, color, r, kind, t0, dur })
      setChips((prev) => [...prev, { id, label, color, pdu }].slice(-5))
    }

    for (const m of messages) {
      const seq = m._seq ?? 0
      if (seq === 0 || seq <= lastSeqRef.current) continue
      lastSeqRef.current = seq
      if (!primedRef.current) continue // don't animate a backlog dump on (re)connect
      const node = nodeOf(m)
      if (!node) continue
      const e = m.event
      const f = m.fields || {}

      /* --- phase-2 UE-to-UE services: hop 1 (UE->BS) --- */
      if (!isBs(node) && (e === svc.APP_MSG_TX || e === svc.APP_CALL_START || e === svc.APP_CALL_END)) {
        const kind: ServiceKind = e === svc.APP_MSG_TX ? 'msg' : kindOf(f, 'voice')
        lastActivePathRef.current = node
        if (f.dst) {
          pendingRef.current.push({ src: ueImsi[node] ?? '', dst: f.dst, kind, landAt: now + KIND_DUR[kind] })
          if (pendingRef.current.length > 24) pendingRef.current.shift()
        }
        const kindLabel = KIND_SHORT[kind]
        const label = e === svc.APP_MSG_TX
          ? `消息: ${(f.text || '').slice(0, 12)}`
          : `${e === svc.APP_CALL_END ? '结束' : '呼叫'}${kindLabel} →${shortImsi(f.dst)}`
        spawn(node, 'ul', KIND_COLOR[kind], KIND_R[kind], kind, KIND_DUR[kind], now, label, null)
        continue
      }

      /* --- phase-2: signaling delivered to a UE (rings / hangups) --- */
      if (!isBs(node) && (e === svc.APP_CALL_INCOMING || e === svc.APP_CALL_PEER_END)) {
        const kind = kindOf(f, 'voice')
        const label = e === svc.APP_CALL_INCOMING
          ? `来电${KIND_SHORT[kind]} ←${shortImsi(f.src)}`
          : `对端挂断${KIND_SHORT[kind]}`
        spawn(node, 'dl', KIND_COLOR[kind], KIND_R[kind], kind, KIND_DUR[kind], now, label, null)
        continue
      }

      /* --- phase-2: BS forwarding = hop 2 (BS->peer UE) --- */
      if (e === svc.APP_FORWARD) {
        const kind = kindOf(f, 'msg')
        const dstPath = imsiToUe(ueImsi, f.dst) ?? lastActivePathRef.current
        const srcPath = imsiToUe(ueImsi, f.src)
        const cnt = parseInt(f.count || '1', 10)
        const kindLabel = KIND_SHORT[kind]
        // Correlate with an in-flight first hop from the sender's TX event:
        // hop 2 leaves the BS exactly when hop 1 lands. With no pending hop
        // (steady media packets carry no per-packet TX event) draw both hops
        // from the forward alone, so streams animate UE->BS->UE.
        let t0 = now
        const pi = pendingRef.current.findIndex((p) =>
          p.dst === f.dst && p.kind === kind && (!p.src || !f.src || p.src === f.src))
        if (pi >= 0) {
          t0 = Math.max(now, pendingRef.current[pi].landAt)
          pendingRef.current.splice(pi, 1)
        } else if (srcPath) {
          spawn(srcPath, 'ul', KIND_COLOR[kind], KIND_R[kind], kind, KIND_DUR[kind], now, `上行${kindLabel}`, null)
          t0 = now + KIND_DUR[kind]
        }
        const label = `转发${kindLabel}${cnt > 1 ? ` x${cnt}` : ''}${f.bytes ? ` ${f.bytes}B` : ''}`
        spawn(dstPath, 'dl', KIND_COLOR[kind], KIND_R[kind], kind, KIND_DUR[kind], t0, label, null)
        continue
      }

      /* hop 2 already drawn via APP_FORWARD; stats are panel-only */
      if (e === svc.APP_MSG_RX || e === svc.APP_STREAM_STATS) continue

      /* --- phase-1 events (unchanged) --- */
      const spec = packetSpec(m, bsPath)
      if (!spec) continue
      if (node !== 'bs') lastActivePathRef.current = spec.path
      const label = m.event === ev.PDU_TRACE
        ? `${m.fields.layer || '?'} ${m.fields.direction || ''} ${m.fields.brief || ''}`.trim()
        : m.event
      spawn(spec.path, spec.dir, spec.color, spec.r, null, FLIGHT_MS, now, label, m.event === ev.PDU_TRACE ? pduFromEvent(m) : null)
    }
    primedRef.current = true
    ensureLoop()
  }, [messages, bsPath, ensureLoop, ueImsi])

  const now = performance.now()
  /** Serving-tower center for a UE link (P8; cell 1 on single-BS streams). */
  const bsPoint = (id: UeId) => BS_POS[mobility[id].serving === '2' ? '2' : '1']
  const endpoint = (p: Packet) => {
    const ue = UE_POS[p.path]
    const bs = bsPoint(p.path)
    const t = Math.min(1, Math.max(0, (now - p.t0) / p.dur))
    const k = p.dir === 'ul' ? t : 1 - t
    return {
      x: ue.x + (bs.x - ue.x) * k,
      y: ue.y + (bs.y - ue.y) * k,
      opacity: t > 0.85 ? Math.max(0, 1 - (t - 0.85) / 0.15) : 1,
    }
  }

  const nodeRing = (n: NodeId) =>
    selected === n ? { stroke: '#e5e7eb', strokeWidth: 3.5 } : {}

  const ueNode = (id: UeId) => {
    const pos = UE_POS[id]
    const nas = ueNas[id]
    const color = stateColor(presence[id] === 'RUNNING' ? nas : 'OFFLINE')
    const ueCalls = Object.values(calls[id] ?? {})
    // radio stack beside the circle (left side for UE2, it hugs the viewBox edge)
    const ri = radio[id]
    const tier = qualityTier(ri.sinr)
    const barColor = tier === 'good' ? '#34d399' : tier === 'mid' ? '#fbbf24' : '#f87171'
    const side = id === 'ue2' ? -1 : 1
    const edgeX = pos.x + side * (UE_R + 5)
    const barsX = side === 1 ? edgeX : edgeX - SIGNAL_BARS_WIDTH
    const textX = side === 1 ? edgeX : edgeX
    const textAnchor = side === 1 ? 'start' : 'end'
    const sleeping = sleep[id].inactive
    return (
      <g onClick={() => onSelect(id)} style={{ cursor: 'pointer' }} opacity={sleeping ? 0.55 : 1}>
        {ueCalls.map((call, i) => (
          <circle key={call.kind} cx={pos.x} cy={pos.y} r={UE_R + 4 + i * 3} fill="none" stroke={KIND_COLOR[call.kind]} strokeWidth={2}>
            <animate attributeName="r" values={`${UE_R + 3 + i * 3};${UE_R + 9 + i * 3};${UE_R + 3 + i * 3}`} dur="1.6s" repeatCount="indefinite" />
            <animate attributeName="stroke-opacity" values="0.9;0.15;0.9" dur="1.6s" repeatCount="indefinite" />
          </circle>
        ))}
        <circle cx={pos.x} cy={pos.y} r={UE_R} fill="#0a0f18" stroke={color} strokeWidth={2.5} {...nodeRing(id)} />
        <text x={pos.x} y={pos.y - 4} textAnchor="middle" fill="#e5e7eb" fontSize={13} fontWeight={700}>{NODE_LABEL[id]}</text>
        <text x={pos.x} y={pos.y + 11} textAnchor="middle" fill={color} fontSize={8.5} fontWeight={600}>{nas}</text>
        {ri.sinr != null && <SignalBars x={barsX} y={pos.y - 8} bars={sinrBars(ri.sinr)} color={barColor} />}
        {mcsLabel(ri.mcs) && (
          <text x={textX} y={pos.y + 4} textAnchor={textAnchor} fill={mcsColor(ri.mcs)} fontSize={8} fontWeight={800} fontFamily="monospace">
            {mcsLabel(ri.mcs)}
          </text>
        )}
        {ri.txDbm != null && (
          <text x={textX} y={pos.y + 14} textAnchor={textAnchor} fill="#6b7280" fontSize={7.5} fontFamily="monospace">
            TX {ri.txDbm}dBm
          </text>
        )}
        <text x={pos.x} y={pos.y + UE_R + 13} textAnchor="middle" fill={sleeping ? '#f59e0b' : '#6b7280'} fontSize={9} fontFamily="monospace">
          {sleeping ? '休眠中 (INACTIVE)' : ueCrnti[id] !== '-' ? `C-RNTI ${ueCrnti[id]}` : presence[id]}
        </text>
        {ueCalls.length > 0 && (
          <text x={pos.x} y={pos.y + UE_R + 25} textAnchor="middle" fontSize={8.5} fontWeight={700}>
            {ueCalls.map((call, i) => (
              <tspan key={call.kind} fill={KIND_COLOR[call.kind]}>
                {i > 0 ? ' + ' : ''}{KIND_SHORT[call.kind]}通话中
              </tspan>
            ))}
          </text>
        )}
      </g>
    )
  }

  const ueCmdBar = (id: UeId) => (
    <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
      <span style={{ fontSize: 10, fontWeight: 800, color: '#6b7280', width: 26 }}>{NODE_LABEL[id]}</span>
      <button style={cmdBtn} onClick={() => onCommand(id, 'attach')}>attach</button>
      <button style={cmdBtn} onClick={() => onCommand(id, 'detach')}>detach</button>
      <button style={cmdBtn} onClick={() => onCommand(id, 'traffic')}>traffic</button>
      <button style={cmdBtn} onClick={() => onCommand(id, 'send hello from lmt')}>send</button>
      {!sleep[id].inactive && <button style={cmdBtn} onClick={() => onCommand(id, 'sleep')}>sleep</button>}
      {sleep[id].inactive && (
        <button style={{ ...cmdBtn, borderColor: 'rgba(16,185,129,0.4)', color: '#10b981' }} onClick={() => onCommand(id, 'wake')}>wake</button>
      )}
    </div>
  )

  /* Phase-2 service buttons. Peer picker: with 3 UEs each phone can dial any
     of the other two; default = first attached peer. Enabled only when the
     UE's own IMSI and the chosen peer's IMSI are both known. */
  const svcCmdBar = (id: UeId) => {
    const peers = UE_NODES.filter((p) => p !== id)
    const sel = peerSel[id]
    const effPeer: UeId | null = ueImsi[sel] ? sel : peers.find((p) => ueImsi[p]) ?? null
    const peerImsi = effPeer ? ueImsi[effPeer] : null
    const voiceCall = calls[id]?.voice
    const videoCall = calls[id]?.video
    const ringing = [voiceCall, videoCall].find((c) => c && !c.established && c.role === 'callee')
    // conference: host gets 结束会议, an idle attached UE with 2 peers gets 会议
    const activeConf = confs.find((c) => c.active) ?? null
    const myConf = activeConf && confUeIds(activeConf, ueImsi).includes(id) ? activeConf : null
    const isConfHost = myConf != null && ueImsi[id] != null && myConf.host === ueImsi[id]
    const confPeers = UE_NODES.filter((p) => p !== id && ueImsi[p])
    const notReady = !ueImsi[id] || !peerImsi
    const svcBtn = (label: string, onClick: () => void, disabled: boolean, activeKind: ServiceKind | null = null) => (
      <button
        style={{
          ...cmdBtn,
          ...(disabled ? { opacity: 0.35, cursor: 'default' } : {}),
          ...(activeKind ? { borderColor: `${KIND_COLOR[activeKind]}88`, color: KIND_COLOR[activeKind] } : {}),
        }}
        disabled={disabled}
        onClick={onClick}
      >
        {label}
      </button>
    )
    const sendMsg = () => {
      const text = msgDraft[id].trim()
      if (!peerImsi || !text) return
      onCommand(id, `msg ${peerImsi} ${text}`)
      setMsgDraft((p) => ({ ...p, [id]: '' }))
    }
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 4, flexWrap: 'wrap' }}>
        <span style={{ fontSize: 10, fontWeight: 800, color: '#6b7280', width: 26 }}>{NODE_LABEL[id]}</span>
        {peers.map((p) => (
          <button
            key={p}
            onClick={() => setPeerSel((prev) => ({ ...prev, [id]: p }))}
            title={`选择对端 ${NODE_LABEL[p]}`}
            style={{
              ...cmdBtn,
              padding: '2px 5px',
              ...(effPeer === p ? { borderColor: 'rgba(59,130,246,0.5)', color: '#60a5fa', background: 'rgba(59,130,246,0.1)' } : {}),
              ...(!ueImsi[p] ? { opacity: 0.35 } : {}),
            }}
          >
            →{NODE_LABEL[p]}
          </button>
        ))}
        {ringing && svcBtn('接听', () => onCommand(id, 'answer'), false)}
        {ringing && svcBtn('拒接', () => onCommand(id, 'decline'), false)}
        {svcBtn(voiceCall ? '挂断' : '打电话',
          () => peerImsi && onCommand(id, voiceCall ? 'call end' : `call ${peerImsi}`),
          notReady, voiceCall ? 'voice' : null)}
        {svcBtn(videoCall ? '结束视频' : '视频',
          () => peerImsi && onCommand(id, videoCall ? 'video end' : `video ${peerImsi}`),
          notReady, videoCall ? 'video' : null)}
        {!myConf && svcBtn('会议',
          () => confPeers.length >= 2 && onCommand(id, `conf ${ueImsi[confPeers[0]]} ${ueImsi[confPeers[1]]}`),
          !ueImsi[id] || confPeers.length < 2 || !!activeConf || !!voiceCall || !!videoCall)}
        {myConf && isConfHost && svcBtn('结束会议', () => onCommand(id, 'conf end'), false, 'conf')}
        <input
          value={msgDraft[id]}
          onChange={(e) => setMsgDraft((p) => ({ ...p, [id]: e.target.value }))}
          onKeyDown={(e) => { if (e.key === 'Enter') sendMsg() }}
          placeholder={notReady ? '等待附着…' : `发给 …${peerImsi!.slice(-4)}`}
          disabled={notReady}
          style={{ ...msgInputStyle, ...(notReady ? { opacity: 0.35 } : {}) }}
        />
        {svcBtn('发消息', sendMsg, notReady || !msgDraft[id].trim())}
      </div>
    )
  }

  return (
    <div>
      <svg viewBox="0 0 420 368" style={{ width: '100%', height: 'auto', display: 'block' }}>
        {/* links — each UE connects to its SERVING gNB; width hints the SINR
            tier; during a handover the new link marches in alongside */}
        {UE_NODES.map((id) => {
          const ue = UE_POS[id]
          const active = ueNas[id] === 'REGISTERED'
          const tier = qualityTier(radio[id].sinr)
          const w = tier === 'good' ? 2.1 : tier === 'poor' ? 1.1 : 1.6
          const mob = mobility[id]
          const bs = bsPoint(id)
          const hoTarget = mob.inFlight && mob.hoTo ? BS_POS[mob.hoTo === '2' ? '2' : '1'] : null
          return (
            <g key={id}>
              <line x1={ue.x} y1={ue.y} x2={bs.x} y2={bs.y}
                stroke={active ? '#10b981' : '#374151'} strokeWidth={w}
                strokeDasharray={active && !mob.inFlight ? 'none' : '5 5'} opacity={mob.inFlight ? 0.4 : 0.7}>
                {active && !mob.inFlight && <animate attributeName="stroke-opacity" values="0.8;0.3;0.8" dur="2s" repeatCount="indefinite" />}
              </line>
              {hoTarget && (
                <line x1={ue.x} y1={ue.y} x2={hoTarget.x} y2={hoTarget.y}
                  stroke={mob.hoTo === '2' ? '#818cf8' : '#60a5fa'} strokeWidth={1.8}
                  strokeDasharray="7 5" opacity={0.9}>
                  <animate attributeName="stroke-dashoffset" values="12;0" dur="0.5s" repeatCount="indefinite" />
                </line>
              )}
            </g>
          )
        })}

        {/* QoS bearer pips at the UE end of each link (sig/voice/video/BE) */}
        {UE_NODES.map((id) => {
          const ue = UE_POS[id]
          const bs = bsPoint(id)
          const dx = bs.x - ue.x
          const dy = bs.y - ue.y
          const len = Math.hypot(dx, dy) || 1
          const ux = dx / len
          const uy = dy / len
          const bx = ue.x + ux * (UE_R + 10)
          const by = ue.y + uy * (UE_R + 10)
          const list = bearers[id] ?? []
          return list.map((cls, i) => {
            const off = (i - (list.length - 1) / 2) * 7.5
            return (
              <circle key={cls} cx={bx - uy * off} cy={by + ux * off} r={2.8}
                fill={BEARER_COLOR[cls]} stroke="#05070a" strokeWidth={0.8}>
                <title>{cls === 'sig' ? 'QCI5 信令' : cls === 'voice' ? 'QCI1 语音' : cls === 'video' ? 'QCI2 视频' : 'QCI9 尽力而为'}</title>
              </circle>
            )
          })
        })}

        {/* channel elements (+ tiny SINR label per link) */}
        {UE_NODES.map((id) => {
          const ue = UE_POS[id]
          const bs = bsPoint(id)
          const m = { x: (ue.x + bs.x) / 2, y: (ue.y + bs.y) / 2 }
          return (
            <g key={id} opacity={0.85}>
              <ellipse cx={m.x} cy={m.y} rx={17} ry={10} fill="rgba(255,255,255,0.02)" stroke="#4b5563" strokeWidth={1} strokeDasharray="3 3" />
              <text x={m.x} y={m.y + 3} textAnchor="middle" fill="#6b7280" fontSize={7.5} fontFamily="monospace">AWGN</text>
              {radio[id].sinr != null && (
                <text x={m.x} y={m.y - 13} textAnchor="middle" fill="#4b5563" fontSize={7.5} fontFamily="monospace">
                  {radio[id].sinr}dB
                </text>
              )}
            </g>
          )
        })}

        {/* packet dots (under the nodes so they slide "into" them) */}
        {packetsRef.current.map((p) => {
          const e = endpoint(p)
          if (p.kind === 'msg') {
            // message "bubble" glyph: rounded rect with a small tail
            return (
              <g key={p.id} opacity={e.opacity}>
                <rect x={e.x - 4.5} y={e.y - 3.5} width={9} height={7} rx={2} fill={p.color} />
                <polygon points={`${e.x - 2},${e.y + 3.5} ${e.x + 1},${e.y + 3.5} ${e.x - 2},${e.y + 6}`} fill={p.color} />
              </g>
            )
          }
          if (p.kind === 'video') {
            // video: larger hollow ring
            return <circle key={p.id} cx={e.x} cy={e.y} r={p.r} fill="none" stroke={p.color} strokeWidth={2.2} opacity={e.opacity} />
          }
          // voice: small rapid solid dot (shorter flight time)
          return <circle key={p.id} cx={e.x} cy={e.y} r={p.r} fill={p.color} opacity={e.opacity} />
        })}

        {/* gNB nodes (BS1 always; BS2 fades in when its process exists) */}
        {([
          { box: BS1, id: 'bs' as NodeId, label: 'gNB', pres: presence.bs, reg: bsRegistered },
          { box: BS2, id: 'bs2' as NodeId, label: 'gNB2', pres: presence.bs2, reg: bs2Registered },
        ]).map(({ box, id, label, pres, reg }) => {
          const exists = id === 'bs' || pres !== 'OFFLINE' || reg !== ''
          return (
            <g key={id} onClick={() => onSelect(id)} style={{ cursor: 'pointer' }} opacity={exists ? 1 : 0.25}>
              <rect x={box.x - box.w / 2} y={box.y - box.h / 2} width={box.w} height={box.h} rx={10}
                fill="#0a0f18" stroke={stateColor(pres)} strokeWidth={2.5} {...nodeRing(id)} />
              <text x={box.x} y={box.y - 4} textAnchor="middle" fill="#e5e7eb" fontSize={13} fontWeight={700}>{label}</text>
              <text x={box.x} y={box.y + 10} textAnchor="middle" fill={stateColor(pres)} fontSize={8} fontWeight={600}>{pres}</text>
              <text x={box.x} y={box.y + 21} textAnchor="middle" fill="#6b7280" fontSize={8} fontFamily="monospace">{reg || '0'} UE(s)</text>
            </g>
          )
        })}

        {UE_NODES.map((id) => (
          <React.Fragment key={id}>{ueNode(id)}</React.Fragment>
        ))}
      </svg>

      {/* per-UE command bars */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginTop: 6 }}>
        {UE_NODES.map((id) => (
          <React.Fragment key={id}>{ueCmdBar(id)}</React.Fragment>
        ))}
      </div>

      {/* per-UE service bars (peer picker + call / video / text) */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginTop: 4 }}>
        {UE_NODES.map((id) => (
          <React.Fragment key={id}>{svcCmdBar(id)}</React.Fragment>
        ))}
      </div>

      {/* latest animated packets */}
      {chips.length > 0 && (
        <div style={{ display: 'flex', gap: 4, marginTop: 8, flexWrap: 'wrap' }}>
          {chips.map((c) => (
            <span
              key={c.id}
              onClick={() => c.pdu && onOpenPdu(c.pdu)}
              title={c.pdu ? 'Open PDU detail' : c.label}
              style={{
                display: 'inline-flex', alignItems: 'center', gap: 5,
                fontSize: 10, fontFamily: 'monospace', color: '#9ca3af',
                background: 'rgba(255,255,255,0.03)', border: '1px solid rgba(255,255,255,0.08)',
                borderRadius: 5, padding: '2px 7px',
                cursor: c.pdu ? 'pointer' : 'default',
                maxWidth: 180, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
              }}
            >
              <span style={{ width: 6, height: 6, borderRadius: '50%', background: c.color, flexShrink: 0 }} />
              {c.label}
            </span>
          ))}
        </div>
      )}
    </div>
  )
}

export default LiveTopology

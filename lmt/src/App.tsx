import React, { useState, useCallback, useEffect, useMemo, useRef } from 'react'
import useWebSocket, { LogEvent } from './hooks/useWebSocket'
import ev from './events'
import { NodeId, nodeOf, NODE_LABEL, UE_NODES } from './nodes'
import LogStream from './components/LogStream'
import LiveTopology from './components/LiveTopology'
import FsmViewer from './components/FsmViewer'
import MscDiagram from './components/MscDiagram'
import PduDetail, { usePduStore, PduEntry } from './components/PduDetail'
import DemoBanner from './components/DemoBanner'
import CallPanel from './components/CallPanel'
import ChatPanel from './components/ChatPanel'
import StoryView from './components/StoryView'
import { svc, deriveCalls, deriveBearers, deriveConfs, deriveRadio, deriveSleep, deriveAuth, deriveMobility, deriveChat, mcsLabel, UeId } from './services'

type RightTab = 'logs' | 'msc' | 'pdu'
type ViewMode = 'ops' | 'story'
type NodePresence = 'OFFLINE' | 'INITIALIZING' | 'RUNNING'
type StreamFilter = 'all' | NodeId

interface UeInfo {
  presence: NodePresence
  mac: string
  rrc: string
  nas: string
  crnti: string
  imsi: string
  tmsi: string
  appRx: string
  traffic: { tx: string; rx: string; loss: string; rtt: string } | null
}

/** Wire name -> FsmViewer display name. */
function macLabel(state: string): string {
  if (state === 'WAIT_CONTENTION_RESOLVE') return 'WAIT_CR'
  return state
}

/** Per-UE state: FSM (D6) + identity + app traffic, all scoped to one node. */
function deriveUe(messages: LogEvent[], node: NodeId): UeInfo {
  const info: UeInfo = {
    presence: 'OFFLINE', mac: 'IDLE', rrc: 'IDLE', nas: 'DEREGISTERED',
    crnti: '-', imsi: '-', tmsi: '-', appRx: '-', traffic: null,
  }
  for (const m of messages) {
    const n = nodeOf(m)
    if (n !== node) {
      // BS-side M20 events carry only c_rnti: fold into the matching UE's RRC
      if (m.fields?.c_rnti && info.crnti !== '-' && m.fields.c_rnti === info.crnti) {
        if (m.event === svc.RRC_INACTIVE) info.rrc = 'INACTIVE'
        else if (m.event === svc.RRC_RESUMED) info.rrc = 'CONNECTED'
      }
      continue
    }
    if (m.event === ev.PROCESS_EXIT) { info.presence = 'OFFLINE'; continue }
    if (m.event === ev.PROCESS_START) { info.presence = 'RUNNING'; continue }
    const next = m.fields?.new || m.fields?.new_state
    if (next) {
      if (m.event === ev.MAC_STATE_CHANGE) info.mac = macLabel(next)
      else if (m.event === ev.RRC_UE_STATE) info.rrc = next
      else if (m.event === ev.NAS_STATE_CHANGE) info.nas = next
    }
    if (m.event === svc.RRC_INACTIVE) info.rrc = 'INACTIVE'
    else if (m.event === svc.RRC_RESUMED) info.rrc = 'CONNECTED'
    if (m.event === ev.UE_STATUS) {
      if (m.fields.c_rnti && m.fields.c_rnti !== '0') info.crnti = m.fields.c_rnti
      if (m.fields.app_rx) info.appRx = m.fields.app_rx
    } else if (m.event === ev.RACH_SUCCESS && m.fields.c_rnti && info.crnti === '-') {
      info.crnti = m.fields.c_rnti
    } else if ((m.event === ev.UE_ATTACH_START || m.event === ev.NAS_ATTACH_REQUEST_TX) && m.fields.imsi) {
      info.imsi = m.fields.imsi
    } else if (m.event === ev.NAS_ATTACH_ACCEPT_RX && m.fields.tmsi) {
      info.tmsi = m.fields.tmsi
    } else if (m.event === ev.TRAFFIC_STATS) {
      info.traffic = {
        tx: m.fields.tx ?? '-', rx: m.fields.rx ?? '-',
        loss: m.fields.loss ?? '-', rtt: m.fields.rtt_avg ?? '-',
      }
    }
  }
  return info
}

/* ------------------------------------------------------------------ */
/* Two-UE mock scenario (no backend needed)                            */
/* ------------------------------------------------------------------ */

interface MockStep { delay: number; node: string; event: string; fields?: Record<string, string> }

const UE_MOCK: Record<UeId, { imsi: string; crnti: string; tmsi: string; preamble: string; sinr: string; rsrp: string; cqi: string; mcs: string }> = {
  ue1: { imsi: '460011234567890', crnti: '4601', tmsi: '70001', preamble: '42', sinr: '28', rsrp: '-78', cqi: '14', mcs: '16qam' }, // good link
  ue2: { imsi: '460011234567891', crnti: '4602', tmsi: '70002', preamble: '43', sinr: '21', rsrp: '-85', cqi: '11', mcs: 'qpsk' }, // mid link
  ue3: { imsi: '460011234567892', crnti: '4603', tmsi: '70003', preamble: '44', sinr: '19', rsrp: '-88', cqi: '9', mcs: 'qpsk' }, // poor link
}

function attachScript(node: UeId, at: number, authFail?: string): MockStep[] {
  const u = UE_MOCK[node]
  const s = (d: number, event: string, fields?: Record<string, string>): MockStep => ({ delay: at + d, node, event, fields })
  const b = (d: number, event: string, fields?: Record<string, string>): MockStep => ({ delay: at + d, node: 'bs', event, fields })
  const registered = node === 'ue1' ? '1' : node === 'ue2' ? '2' : '3'
  const sh = authFail ? 300 : 0 // a failed first auth shifts the tail of the sequence
  const steps: MockStep[] = [
    s(0, ev.PROCESS_START, { msg: `${node} starting up` }),
    s(200, ev.RRC_MIB_RX, { sfn: '0', bw: '50' }),
    s(400, ev.RRC_SIB1_RX, { plmn: '46001', tac: '1', cell_id: '1' }),
    s(600, ev.UE_ATTACH_START, { imsi: u.imsi }),
    s(800, ev.MAC_STATE_CHANGE, { layer: 'MAC', old_state: 'IDLE', new_state: 'WAIT_RAR' }),
    s(900, ev.MAC_RACH_MSG1, { preamble: u.preamble, tx_count: '1' }),
    b(1100, ev.MAC_RACH_MSG2, { ra_rnti: '17194', ta: '12' }),
    s(1200, ev.RACH_SUCCESS, { c_rnti: u.crnti }),
    s(1300, ev.MAC_STATE_CHANGE, { layer: 'MAC', old_state: 'WAIT_CR', new_state: 'CONNECTED' }),
    s(1400, ev.RRC_UE_STATE, { old: 'IDLE', new: 'CONNECTING' }),
    s(1450, ev.RRC_SETUP_REQUEST_TX, { c_rnti: u.crnti }),
    b(1600, ev.RRC_SETUP_TX, { c_rnti: u.crnti }),
    s(1700, ev.RRC_SETUP_RX, { c_rnti: u.crnti }),
    s(1800, ev.RRC_SETUP_COMPLETE_TX, { c_rnti: u.crnti }),
    s(1900, ev.RRC_UE_STATE, { old: 'CONNECTING', new: 'CONNECTED' }),
    s(1950, ev.NAS_STATE_CHANGE, { old: 'DEREGISTERED', new: 'REGISTERING' }),
    s(2000, ev.NAS_ATTACH_REQUEST_TX, { imsi: u.imsi }),
  ]
  // P7 demo beat: first attempt rejected (mac), fast retry succeeds
  if (authFail) {
    steps.push(
      b(2050, svc.NAS_AUTH_VECTOR, { imsi: u.imsi, rand: mockHex(8), sqn_masked: '0000**2f' }),
      s(2100, svc.NAS_AUTH_RES, { imsi: u.imsi, res: mockHex(8) }),
      b(2130, svc.NAS_AUTH_FAIL, { imsi: u.imsi, cause: authFail }),
      s(2180, ev.NAS_STATE_CHANGE, { old: 'REGISTERING', new: 'DEREGISTERED' }),
      s(2250, ev.NAS_STATE_CHANGE, { old: 'DEREGISTERED', new: 'REGISTERING' }),
      s(2300, ev.NAS_ATTACH_REQUEST_TX, { imsi: u.imsi }),
    )
  }
  steps.push(
    // 5G-AKA: challenge down, response up, success — then attach accept
    b(2050 + sh, svc.NAS_AUTH_VECTOR, { imsi: u.imsi, rand: mockHex(8), sqn_masked: '0000**31' }),
    s(2100 + sh, svc.NAS_AUTH_RES, { imsi: u.imsi, res: mockHex(8) }),
    b(2130 + sh, svc.NAS_AUTH_SUCCESS, { imsi: u.imsi }),
    b(2150 + sh, ev.NAS_ATTACH_ACCEPT_TX, { c_rnti: u.crnti }),
    s(2250 + sh, ev.NAS_ATTACH_ACCEPT_RX, { tmsi: u.tmsi }),
    s(2350 + sh, ev.NAS_STATE_CHANGE, { old: 'REGISTERING', new: 'REGISTERED' }),
    s(2400 + sh, ev.UE_STATUS, { c_rnti: u.crnti, app_rx: '0' }),
    s(2450 + sh, svc.QOS_BEARER_SETUP, { c_rnti: u.crnti, qci: '5', kind: 'sig' }), // signaling bearer up after attach
    // M19 radio telemetry: per-link quality, CQI, MCS selection, TX power
    s(2520 + sh, svc.LINK_QUALITY, { c_rnti: u.crnti, rsrp: u.rsrp, sinr: u.sinr }),
    b(2560 + sh, svc.CQI_REPORT, { c_rnti: u.crnti, cqi: u.cqi, snr_db: u.sinr }),
    b(2600 + sh, svc.MCS_CHANGE, { c_rnti: u.crnti, mcs: u.mcs, direction: 'dl' }),
    s(2640 + sh, svc.TX_POWER_CHANGE, { c_rnti: u.crnti, dbm: node === 'ue3' ? '1' : node === 'ue2' ? '0' : '-2' }),
    b(2700 + sh, ev.BS_STATUS, { registered_ues: registered }),
  )
  return steps
}

const MOCK_SCRIPT: MockStep[] = [
  { delay: 0, node: 'bs', event: ev.PROCESS_START, fields: { msg: 'BS active on cell 1' } },
  { delay: 200, node: 'bs', event: ev.PHY_CONFIG, fields: { n_fft: '64', cp_len: '16' } },
  { delay: 300, node: 'bs', event: ev.BS_SIB_BROADCAST_ON, fields: { period_ms: '200' } },
  { delay: 120, node: 'bs2', event: ev.PROCESS_START, fields: { msg: 'BS2 active on cell 2' } },
  { delay: 260, node: 'bs2', event: ev.PHY_CONFIG, fields: { n_fft: '64', cp_len: '16' } },
  { delay: 340, node: 'bs2', event: ev.BS_SIB_BROADCAST_ON, fields: { period_ms: '200' } },
  ...attachScript('ue1', 600),
  ...attachScript('ue2', 4200),
  ...attachScript('ue3', 7800, 'mac'), // UE3 joins later — and its first AKA fails, then retries
]
const MOCK_SCRIPT_END = 7000
/** Phase-3 services story (voice call, texts, video) starts this many ms in. */
const T_STORY = 12000

function mockHex(len: number): string {
  let out = ''
  for (let i = 0; i < len; i++) out += Math.floor(Math.random() * 256).toString(16).padStart(2, '0')
  return out
}

/* ------------------------------------------------------------------ */

export const App: React.FC = () => {
  const { messages, isConnected, clearMessages, setMessages, sendCommand } = useWebSocket('ws://localhost:8765')
  const [mocking, setMocking] = useState(false)
  const mockRef = useRef<{ timers: ReturnType<typeof setTimeout>[]; intervals: ReturnType<typeof setInterval>[] }>({ timers: [], intervals: [] })
  const [rightTab, setRightTab] = useState<RightTab>('logs')
  const [view, setView] = useState<ViewMode>('ops')
  const [selected, setSelected] = useState<NodeId>('ue1')
  const [streamFilter, setStreamFilter] = useState<StreamFilter>('all')
  const { pdus, selectedPdu, setSelectedPdu, addEvents } = usePduStore()

  // Feed the PDU store only genuinely new events (server-side _seq).
  useEffect(() => {
    addEvents(messages)
  }, [messages, addEvents])

  const ue1 = useMemo(() => deriveUe(messages, 'ue1'), [messages])
  const ue2 = useMemo(() => deriveUe(messages, 'ue2'), [messages])
  const ue3 = useMemo(() => deriveUe(messages, 'ue3'), [messages])

  // IMSI is the "phone number" for UE-to-UE services; dialing stays disabled
  // until the UE and its chosen peer have attached and revealed theirs.
  const ueImsi = useMemo((): Record<UeId, string | null> => ({
    ue1: ue1.imsi !== '-' ? ue1.imsi : null,
    ue2: ue2.imsi !== '-' ? ue2.imsi : null,
    ue3: ue3.imsi !== '-' ? ue3.imsi : null,
  }), [ue1.imsi, ue2.imsi, ue3.imsi])

  const calls = useMemo(() => deriveCalls(messages, ueImsi), [messages, ueImsi])
  const bearers = useMemo(
    () => deriveBearers(messages, { ue1: ue1.crnti, ue2: ue2.crnti, ue3: ue3.crnti }),
    [messages, ue1.crnti, ue2.crnti, ue3.crnti],
  )
  const confs = useMemo(() => deriveConfs(messages), [messages])
  const radio = useMemo(
    () => deriveRadio(messages, { ue1: ue1.crnti, ue2: ue2.crnti, ue3: ue3.crnti }),
    [messages, ue1.crnti, ue2.crnti, ue3.crnti],
  )
  const sleep = useMemo(
    () => deriveSleep(messages, { ue1: ue1.crnti, ue2: ue2.crnti, ue3: ue3.crnti }, ueImsi),
    [messages, ue1.crnti, ue2.crnti, ue3.crnti, ueImsi],
  )
  const auth = useMemo(() => deriveAuth(messages, ueImsi), [messages, ueImsi])
  const mobility = useMemo(
    () => deriveMobility(messages, ueImsi, { ue1: ue1.crnti, ue2: ue2.crnti, ue3: ue3.crnti }),
    [messages, ueImsi, ue1.crnti, ue2.crnti, ue3.crnti],
  )
  const chat = useMemo(() => deriveChat(messages, ueImsi), [messages, ueImsi])

  const presence = useMemo((): Record<NodeId, NodePresence> => {
    let bs: NodePresence = 'OFFLINE'
    let bs2: NodePresence = 'OFFLINE'
    for (const m of messages) {
      const n = nodeOf(m)
      if (n === 'bs') {
        if (m.event === ev.PROCESS_EXIT) bs = 'OFFLINE'
        else if (m.event === ev.PROCESS_START) bs = 'RUNNING'
      } else if (n === 'bs2') {
        if (m.event === ev.PROCESS_EXIT) bs2 = 'OFFLINE'
        else if (m.event === ev.PROCESS_START) bs2 = 'RUNNING'
      }
    }
    return { ue1: ue1.presence, ue2: ue2.presence, ue3: ue3.presence, bs, bs2 }
  }, [messages, ue1.presence, ue2.presence, ue3.presence])

  // Per-gNB telemetry: cell config + registered UE count (bs2 only when a
  // second tower exists; UE-side SIB/PHY broadcasts attribute to bs1 as before).
  const bsInfo = useMemo(() => {
    const mk = () => ({
      phy: { n_fft: '-', cp_len: '-' },
      sib: null as { plmn: string; tac: string; cell_id: string } | null,
      registered: '',
    })
    const out = { bs: mk(), bs2: mk() }
    let bs2Seen = false
    for (const m of messages) {
      const n = nodeOf(m)
      if (n === 'bs2') bs2Seen = true
      const t = n === 'bs2' ? out.bs2 : out.bs
      if (m.event === ev.PHY_CONFIG && m.fields.n_fft) t.phy = { n_fft: m.fields.n_fft, cp_len: m.fields.cp_len ?? '-' }
      else if (m.event === ev.RRC_SIB1_RX) t.sib = { plmn: m.fields.plmn ?? '-', tac: m.fields.tac ?? '-', cell_id: m.fields.cell_id ?? '-' }
      else if ((m.event === ev.BS_STATUS || m.event === ev.HEARTBEAT) && (n === 'bs' || n === 'bs2') && m.fields.registered_ues) {
        t.registered = m.fields.registered_ues
      }
    }
    if (!out.bs.registered) out.bs.registered = String((ue1.nas === 'REGISTERED' ? 1 : 0) + (ue2.nas === 'REGISTERED' ? 1 : 0) + (ue3.nas === 'REGISTERED' ? 1 : 0))
    if (!out.bs2.registered) out.bs2.registered = bs2Seen ? String(UE_NODES.filter((id) => mobility[id].serving === '2').length) : ''
    return out
  }, [messages, ue1.nas, ue2.nas, ue3.nas, mobility])

  const stopMock = useCallback(() => {
    mockRef.current.timers.forEach(clearTimeout)
    mockRef.current.timers = []
    mockRef.current.intervals.forEach(clearInterval)
    mockRef.current.intervals = []
    setMocking(false)
  }, [])

  const startMock = useCallback(() => {
    setMocking(true)
    let seq = Date.now() % 100000 // mock _seq stream so the PDU store works offline
    const push = (node: string, event: string, fields: Record<string, string> = {}) => {
      const evt: LogEvent = {
        timestamp: new Date().toISOString(),
        module: node === 'bs' || node === 'bs2' ? 'BS' : 'UE',
        node,
        level: 'INFO',
        event,
        fields,
        _seq: ++seq,
      }
      setMessages((prev) => [...prev, evt].slice(-500))
    }

    // Phase 1: scripted boot + both UE attaches.
    for (const step of MOCK_SCRIPT) {
      mockRef.current.timers.push(setTimeout(() => push(step.node, step.event, step.fields), step.delay))
    }

    // Phase 2: steady-state traffic on both links.
    const counters: Record<'ue1' | 'ue2', { tx: number; rx: number; loss: number }> = {
      ue1: { tx: 0, rx: 0, loss: 0 },
      ue2: { tx: 0, rx: 0, loss: 0 },
    }
    let tick = 0
    mockRef.current.timers.push(setTimeout(() => {
      mockRef.current.intervals.push(setInterval(() => {
        tick++
        const node = tick % 2 === 0 ? 'ue2' : 'ue1'
        const c = counters[node]
        c.tx++
        push(node, ev.APP_DATA_TX, { seq: String(c.tx), len: '32' })
        push(node, ev.PDU_TRACE, { layer: 'PDCP', direction: 'TX', len: '32', hex: mockHex(32), brief: `UL data seq=${c.tx}` })
        push('bs', ev.PDU_TRACE, { layer: 'PDCP', direction: 'TX', len: '32', hex: mockHex(32), brief: `DL echo seq=${c.tx}`, c_rnti: UE_MOCK[node].crnti })
        if (tick % 13 === 0) {
          c.loss++
          push(node, ev.APP_LOSS, { seq: String(c.tx) })
        } else {
          c.rx++
          push(node, ev.APP_RTT, { seq: String(c.tx), rtt_ms: String(2 + Math.floor(Math.random() * 7)) })
        }
        if (tick % 6 === 0) {
          for (const u of ['ue1', 'ue2'] as const) {
            const k = counters[u]
            push(u, ev.TRAFFIC_STATS, { tx: String(k.tx), rx: String(k.rx), loss: String(k.loss), rtt_min: '2', rtt_max: '9', rtt_avg: '4' })
            push(u, ev.HEARTBEAT, { c_rnti: UE_MOCK[u].crnti, registered: '1' })
          }
          push('bs', ev.BS_STATUS, { registered_ues: '2' })
        }
      }, 900))
    }, MOCK_SCRIPT_END))

    // Phase 3: UE-to-UE services story (SIP-lite dialogs) with three UEs.
    // Every beat emits the SIP_* event AND its legacy APP_* mapping, exactly
    // like the backend: APP_CALL_START at INVITE, INCOMING at INVITE receipt,
    // END on local hangup/603, PEER_END on BYE/CANCEL received.
    const imsi1 = UE_MOCK.ue1.imsi
    const imsi2 = UE_MOCK.ue2.imsi
    const imsi3 = UE_MOCK.ue3.imsi
    const at = (d: number, fn: () => void) => {
      mockRef.current.timers.push(setTimeout(fn, T_STORY + d))
    }
    const vc = { tx1: 0, rx1: 0, loss1: 0, tx2: 0, rx2: 0, loss2: 0 }
    const dc = { tx1: 0, rx1: 0, tx3: 0, rx3: 0 }
    let voiceIv: ReturnType<typeof setInterval> | null = null
    let videoIv: ReturnType<typeof setInterval> | null = null
    const fwd = (src: string, dst: string, kind: string, bytes: string) =>
      push('bs', svc.APP_FORWARD, { src, dst, kind, bytes })

    // M19 radio beats: LINK_QUALITY refresh round, ue1 mid-call MCS dip and
    // recovery, ue3 open-loop + TPC power climb across the story
    at(5000, () => {
      for (const u of ['ue1', 'ue2', 'ue3'] as const) {
        push(u, svc.LINK_QUALITY, { c_rnti: UE_MOCK[u].crnti, rsrp: UE_MOCK[u].rsrp, sinr: UE_MOCK[u].sinr })
      }
    })
    at(8000, () => push('ue3', svc.TX_POWER_CHANGE, { c_rnti: UE_MOCK.ue3.crnti, dbm: '2' }))
    at(30000, () => { // ue1's link dips mid video-call: 16QAM -> QPSK
      push('ue1', svc.LINK_QUALITY, { c_rnti: UE_MOCK.ue1.crnti, rsrp: '-82', sinr: '23' })
      push('bs', svc.MCS_CHANGE, { c_rnti: UE_MOCK.ue1.crnti, mcs: 'qpsk', direction: 'dl' })
    })
    at(36000, () => { // ... and recovers
      push('ue1', svc.LINK_QUALITY, { c_rnti: UE_MOCK.ue1.crnti, rsrp: UE_MOCK.ue1.rsrp, sinr: UE_MOCK.ue1.sinr })
      push('bs', svc.MCS_CHANGE, { c_rnti: UE_MOCK.ue1.crnti, mcs: '16qam', direction: 'dl' })
    })
    at(48000, () => push('ue3', svc.TX_POWER_CHANGE, { c_rnti: UE_MOCK.ue3.crnti, dbm: '3' }))
    at(80000, () => push('ue3', svc.TX_POWER_CHANGE, { c_rnti: UE_MOCK.ue3.crnti, dbm: '4' }))

    // --- call 1: ue1 voice-calls ue2; INVITE → 1.2 s → 180 → 1.5 s → answer ---
    at(0, () => { push('ue1', svc.SIP_INVITE_TX, { dst: imsi2, kind: 'voice' }); push('ue1', svc.APP_CALL_START, { dst: imsi2, kind: 'voice' }) })
    at(150, () => fwd(imsi1, imsi2, 'voice', '64'))
    at(300, () => { push('ue2', svc.SIP_INVITE_RX, { src: imsi1, kind: 'voice' }); push('ue2', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'voice' }) })
    at(1500, () => push('ue2', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(1650, () => fwd(imsi2, imsi1, 'voice', '48'))
    at(1800, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi2 }))
    at(3300, () => push('ue2', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'voice' }))
    at(3450, () => fwd(imsi2, imsi1, 'voice', '52'))
    at(3600, () => push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi2, kind: 'voice' }))
    at(3900, () => {
      let n = 0
      voiceIv = setInterval(() => {
        n++
        const from1 = n % 2 === 1
        if (from1) { vc.tx1++; vc.rx2++ } else { vc.tx2++; vc.rx1++ }
        if (n % 29 === 0) { if (from1) vc.loss1++; else vc.loss2++ }
        push('bs', svc.APP_FORWARD, {
          src: from1 ? imsi1 : imsi2, dst: from1 ? imsi2 : imsi1,
          kind: 'voice', bytes: '160', count: '4',
        })
      }, 350)
      mockRef.current.intervals.push(voiceIv)
    })
    const pushVoiceStats = () => {
      push('ue1', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi2, tx: String(vc.tx1), rx: String(vc.rx1), loss: String(vc.loss1), rtt_avg: '18' })
      push('ue2', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi1, tx: String(vc.tx2), rx: String(vc.rx2), loss: String(vc.loss2), rtt_avg: '17' })
    }
    at(7000, pushVoiceStats)
    at(11000, pushVoiceStats)
    // mid-call text from UE3 to UE1 (while the voice call is live)
    at(7000, () => push('ue3', svc.APP_MSG_TX, { dst: imsi1, text: 'UE3 上线啦，打扰一下' }))
    at(7150, () => fwd(imsi3, imsi1, 'msg', '38'))
    at(7300, () => push('ue1', svc.APP_MSG_RX, { src: imsi3, text: 'UE3 上线啦，打扰一下' }))
    // ue1 hangs up: BYE both ways
    at(12000, () => {
      if (voiceIv) clearInterval(voiceIv)
      push('ue1', svc.SIP_BYE_TX, { peer: imsi2 })
      push('ue1', svc.APP_CALL_END, { dst: imsi2, kind: 'voice' })
    })
    at(12150, () => fwd(imsi1, imsi2, 'voice', '48'))
    at(12300, () => { push('ue2', svc.SIP_BYE_RX, { peer: imsi1 }); push('ue2', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'voice' }) })

    // text recap
    at(13500, () => push('ue1', svc.APP_MSG_TX, { dst: imsi2, text: '通话质量不错' }))
    at(13650, () => fwd(imsi1, imsi2, 'msg', '28'))
    at(13800, () => push('ue2', svc.APP_MSG_RX, { src: imsi1, text: '通话质量不错' }))

    // --- call 2: ue3 calls ue1; ue1 rings, then declines (603) ---
    at(15500, () => { push('ue3', svc.SIP_INVITE_TX, { dst: imsi1, kind: 'voice' }); push('ue3', svc.APP_CALL_START, { dst: imsi1, kind: 'voice' }) })
    at(15650, () => fwd(imsi3, imsi1, 'voice', '64'))
    at(15800, () => { push('ue1', svc.SIP_INVITE_RX, { src: imsi3, kind: 'voice' }); push('ue1', svc.APP_CALL_INCOMING, { src: imsi3, kind: 'voice' }) })
    at(17000, () => push('ue1', svc.SIP_RINGING_TX, { dst: imsi3 }))
    at(17150, () => fwd(imsi1, imsi3, 'voice', '48'))
    at(17300, () => push('ue3', svc.SIP_RINGING_RX, { src: imsi1 }))
    at(18500, () => push('ue1', svc.APP_CALL_END, { dst: imsi3, kind: 'voice' })) // 603 Decline
    at(18650, () => fwd(imsi1, imsi3, 'voice', '52'))
    at(18800, () => { push('ue3', svc.SIP_CALL_FAILED, { peer: imsi1, reason: 'declined' }); push('ue3', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'voice' }) })

    // --- call 3: ue1 video-calls ue3; ue2 tries ue1 mid-call and gets 486 ---
    at(21000, () => { push('ue1', svc.SIP_INVITE_TX, { dst: imsi3, kind: 'video' }); push('ue1', svc.APP_CALL_START, { dst: imsi3, kind: 'video' }) })
    at(21150, () => fwd(imsi1, imsi3, 'video', '220'))
    at(21300, () => { push('ue3', svc.SIP_INVITE_RX, { src: imsi1, kind: 'video' }); push('ue3', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'video' }) })
    at(22500, () => push('ue3', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(22650, () => fwd(imsi3, imsi1, 'video', '48'))
    at(22800, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi3 }))
    at(24000, () => push('ue3', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'video' }))
    at(24150, () => fwd(imsi3, imsi1, 'video', '52'))
    at(24300, () => push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi3, kind: 'video' }))
    at(24600, () => {
      let n = 0
      videoIv = setInterval(() => {
        n++
        const from1 = n % 3 !== 0 // downlink-heavy, like a real stream
        if (from1) { dc.tx1++; dc.rx3++ } else { dc.tx3++; dc.rx1++ }
        push('bs', svc.APP_FORWARD, {
          src: from1 ? imsi1 : imsi3, dst: from1 ? imsi3 : imsi1,
          kind: 'video', bytes: '1400', count: '8',
        })
      }, 250)
      mockRef.current.intervals.push(videoIv)
    })
    // busy attempt: ue2 dials ue1 while ue1<->ue3 is established
    at(27000, () => { push('ue2', svc.SIP_INVITE_TX, { dst: imsi1, kind: 'voice' }); push('ue2', svc.APP_CALL_START, { dst: imsi1, kind: 'voice' }) })
    at(27150, () => fwd(imsi2, imsi1, 'voice', '64'))
    at(27300, () => { push('ue2', svc.SIP_CALL_FAILED, { peer: imsi1, reason: 'busy' }); push('ue2', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'voice' }) })
    const pushVideoStats = () => {
      push('ue1', svc.APP_STREAM_STATS, { kind: 'video', peer: imsi3, tx: String(dc.tx1), rx: String(dc.rx1), loss: '0', rtt_avg: '22' })
      push('ue3', svc.APP_STREAM_STATS, { kind: 'video', peer: imsi1, tx: String(dc.tx3), rx: String(dc.rx3), loss: '1', rtt_avg: '23' })
    }
    at(28000, pushVideoStats)
    at(32000, pushVideoStats)
    at(34000, () => {
      if (videoIv) clearInterval(videoIv)
      push('ue1', svc.SIP_BYE_TX, { peer: imsi3 })
      push('ue1', svc.APP_CALL_END, { dst: imsi3, kind: 'video' })
    })
    at(34150, () => fwd(imsi1, imsi3, 'video', '60'))
    at(34300, () => { push('ue3', svc.SIP_BYE_RX, { peer: imsi1 }); push('ue3', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'video' }) })

    // closing texts
    at(35500, () => push('ue3', svc.APP_MSG_TX, { dst: imsi2, text: '视频很清晰，演示结束' }))
    at(35650, () => fwd(imsi3, imsi2, 'msg', '40'))
    at(35800, () => push('ue2', svc.APP_MSG_RX, { src: imsi3, text: '视频很清晰，演示结束' }))
    at(36500, () => push('ue1', svc.APP_MSG_TX, { dst: imsi3, text: '下次再聊' }))
    at(36650, () => fwd(imsi1, imsi3, 'msg', '20'))
    at(36800, () => push('ue3', svc.APP_MSG_RX, { src: imsi1, text: '下次再聊' }))

    // --- call 4: QoS concurrent stretch — ue1 runs a VIDEO call with ue2 and
    // a VOICE call with ue3 at the same time (dedicated bearers, per-kind
    // stats with distinct qci, staggered hangups + teardowns) ---
    const cc = { vtx1: 0, vrx1: 0, vtx2: 0, vrx2: 0, wtx1: 0, wrx1: 0, wtx3: 0, wrx3: 0 }
    let concVideoIv: ReturnType<typeof setInterval> | null = null
    let concVoiceIv: ReturnType<typeof setInterval> | null = null
    const qos = (node: string, event: string, u: UeId, qci: string, kind: string) =>
      push(node, event, { c_rnti: UE_MOCK[u].crnti, qci, kind })

    // video: ue1 -> ue2
    at(40000, () => { push('ue1', svc.SIP_INVITE_TX, { dst: imsi2, kind: 'video' }); push('ue1', svc.APP_CALL_START, { dst: imsi2, kind: 'video' }) })
    at(40150, () => fwd(imsi1, imsi2, 'video', '220'))
    at(40300, () => { push('ue2', svc.SIP_INVITE_RX, { src: imsi1, kind: 'video' }); push('ue2', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'video' }) })
    at(41500, () => push('ue2', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(41650, () => fwd(imsi2, imsi1, 'video', '48'))
    at(41800, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi2 }))
    at(43000, () => {
      push('ue2', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'video' })
      qos('ue2', svc.QOS_BEARER_SETUP, 'ue2', '2', 'video')
    })
    at(43150, () => fwd(imsi2, imsi1, 'video', '52'))
    at(43300, () => {
      push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi2, kind: 'video' })
      qos('ue1', svc.QOS_BEARER_SETUP, 'ue1', '2', 'video')
    })
    at(43600, () => {
      let n = 0
      concVideoIv = setInterval(() => {
        n++
        const from1 = n % 3 !== 0
        if (from1) { cc.vtx1++; cc.vrx2++ } else { cc.vtx2++; cc.vrx1++ }
        push('bs', svc.APP_FORWARD, {
          src: from1 ? imsi1 : imsi2, dst: from1 ? imsi2 : imsi1,
          kind: 'video', bytes: '1400', count: '8',
        })
      }, 250)
      mockRef.current.intervals.push(concVideoIv)
    })
    // voice on top: ue1 -> ue3 (while the video call is live)
    at(47000, () => { push('ue1', svc.SIP_INVITE_TX, { dst: imsi3, kind: 'voice' }); push('ue1', svc.APP_CALL_START, { dst: imsi3, kind: 'voice' }) })
    at(47150, () => fwd(imsi1, imsi3, 'voice', '64'))
    at(47300, () => { push('ue3', svc.SIP_INVITE_RX, { src: imsi1, kind: 'voice' }); push('ue3', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'voice' }) })
    at(48500, () => push('ue3', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(48650, () => fwd(imsi3, imsi1, 'voice', '48'))
    at(48800, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi3 }))
    at(50000, () => {
      push('ue3', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'voice' })
      qos('bs', svc.QOS_BEARER_SETUP, 'ue3', '1', 'voice') // BS-side setup, c_rnti-mapped
    })
    at(50150, () => fwd(imsi3, imsi1, 'voice', '52'))
    at(50300, () => {
      push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi3, kind: 'voice' })
      qos('ue1', svc.QOS_BEARER_SETUP, 'ue1', '1', 'voice')
    })
    at(50600, () => {
      let n = 0
      concVoiceIv = setInterval(() => {
        n++
        const from1 = n % 2 === 1
        if (from1) { cc.wtx1++; cc.wrx3++ } else { cc.wtx3++; cc.wrx1++ }
        push('bs', svc.APP_FORWARD, {
          src: from1 ? imsi1 : imsi3, dst: from1 ? imsi3 : imsi1,
          kind: 'voice', bytes: '160', count: '4',
        })
      }, 350)
      mockRef.current.intervals.push(concVoiceIv)
    })
    // interleaved per-bearer stats: ue1 reports video (qci 2) AND voice (qci 1)
    const pushConcStats = () => {
      push('ue1', svc.APP_STREAM_STATS, { kind: 'video', peer: imsi2, qci: '2', tx: String(cc.vtx1), rx: String(cc.vrx1), loss: '0', rtt_avg: '24' })
      push('ue2', svc.APP_STREAM_STATS, { kind: 'video', peer: imsi1, qci: '2', tx: String(cc.vtx2), rx: String(cc.vrx2), loss: '1', rtt_avg: '25' })
      push('ue1', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi3, qci: '1', tx: String(cc.wtx1), rx: String(cc.wrx1), loss: '0', rtt_avg: '16' })
      push('ue3', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi1, qci: '1', tx: String(cc.wtx3), rx: String(cc.wrx3), loss: '0', rtt_avg: '15' })
    }
    at(52000, pushConcStats)
    at(56000, pushConcStats)
    // staggered hangup 1: the video call ends, voice keeps going
    at(58000, () => {
      if (concVideoIv) clearInterval(concVideoIv)
      push('ue1', svc.SIP_BYE_TX, { peer: imsi2 })
      push('ue1', svc.APP_CALL_END, { dst: imsi2, kind: 'video' })
      qos('ue1', svc.QOS_BEARER_TEARDOWN, 'ue1', '2', 'video')
    })
    at(58150, () => fwd(imsi1, imsi2, 'video', '60'))
    at(58300, () => {
      push('ue2', svc.SIP_BYE_RX, { peer: imsi1 })
      push('ue2', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'video' })
      qos('ue2', svc.QOS_BEARER_TEARDOWN, 'ue2', '2', 'video')
    })
    at(60000, () => {
      push('ue1', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi3, qci: '1', tx: String(cc.wtx1), rx: String(cc.wrx1), loss: '0', rtt_avg: '16' })
      push('ue3', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi1, qci: '1', tx: String(cc.wtx3), rx: String(cc.wrx3), loss: '0', rtt_avg: '15' })
    })
    // staggered hangup 2: the voice call ends
    at(63000, () => {
      if (concVoiceIv) clearInterval(concVoiceIv)
      push('ue1', svc.SIP_BYE_TX, { peer: imsi3 })
      push('ue1', svc.APP_CALL_END, { dst: imsi3, kind: 'voice' })
      qos('ue1', svc.QOS_BEARER_TEARDOWN, 'ue1', '1', 'voice')
    })
    at(63150, () => fwd(imsi1, imsi3, 'voice', '48'))
    at(63300, () => {
      push('ue3', svc.SIP_BYE_RX, { peer: imsi1 })
      push('ue3', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'voice' })
      qos('ue3', svc.QOS_BEARER_TEARDOWN, 'ue3', '1', 'voice')
    })
    at(64500, () => push('ue1', svc.APP_MSG_TX, { dst: imsi2, text: '双路并发，QoS 调度正常' }))
    at(64650, () => fwd(imsi1, imsi2, 'msg', '34'))
    at(64800, () => push('ue2', svc.APP_MSG_RX, { src: imsi1, text: '双路并发，QoS 调度正常' }))

    // --- call 5: 3-party conference (P4, BS audio bridge) — ue1 hosts ue2+ue3.
    // Under the hood each party is an ordinary SIP voice dialog; CONF_* tracks
    // membership; media is one conf stream per member fanned out by the bridge.
    const CONF_ID = '9001'
    const cf: Record<'ue1' | 'ue2' | 'ue3', { tx: number; rx: number }> = {
      ue1: { tx: 0, rx: 0 }, ue2: { tx: 0, rx: 0 }, ue3: { tx: 0, rx: 0 },
    }
    let confParties: ('ue1' | 'ue2' | 'ue3')[] = ['ue1', 'ue2', 'ue3']
    let confIv: ReturnType<typeof setInterval> | null = null
    const imsiOf = (u: 'ue1' | 'ue2' | 'ue3') => UE_MOCK[u].imsi
    const pushConfStats = () => {
      for (const p of confParties) {
        push(p, svc.APP_STREAM_STATS, {
          kind: 'conf', qci: '1', conf_id: CONF_ID,
          tx: String(cf[p].tx), rx: String(cf[p].rx), loss: '0', rtt_avg: '14',
        })
      }
    }

    at(68000, () => {
      push('ue1', svc.SIP_INVITE_TX, { dst: imsi2, kind: 'voice' })
      push('ue1', svc.APP_CALL_START, { dst: imsi2, kind: 'voice' })
      push('ue1', svc.CONF_START, { host: imsi1, conf_id: CONF_ID })
    })
    at(68120, () => {
      push('ue1', svc.SIP_INVITE_TX, { dst: imsi3, kind: 'voice' })
      push('ue1', svc.APP_CALL_START, { dst: imsi3, kind: 'voice' })
    })
    at(68270, () => fwd(imsi1, imsi2, 'voice', '64'))
    at(68400, () => fwd(imsi1, imsi3, 'voice', '64'))
    at(68550, () => { push('ue2', svc.SIP_INVITE_RX, { src: imsi1, kind: 'voice' }); push('ue2', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'voice' }) })
    at(68700, () => { push('ue3', svc.SIP_INVITE_RX, { src: imsi1, kind: 'voice' }); push('ue3', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'voice' }) })
    at(69900, () => push('ue2', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(70050, () => fwd(imsi2, imsi1, 'voice', '48'))
    at(70200, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi2 }))
    at(70400, () => push('ue3', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(70550, () => fwd(imsi3, imsi1, 'voice', '48'))
    at(70700, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi3 }))
    // ue2 answers and joins
    at(71600, () => {
      push('ue2', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'voice' })
      push('ue2', svc.CONF_JOIN, { conf_id: CONF_ID, imsi: imsi2 })
    })
    at(71750, () => fwd(imsi2, imsi1, 'voice', '52'))
    at(71900, () => push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi2, kind: 'voice' }))
    // ue3 answers and joins
    at(73100, () => {
      push('ue3', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'voice' })
      push('ue3', svc.CONF_JOIN, { conf_id: CONF_ID, imsi: imsi3 })
    })
    at(73250, () => fwd(imsi3, imsi1, 'voice', '52'))
    at(73400, () => push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi3, kind: 'voice' }))
    // bridge media: each member's conf stream fanned out to the other members
    at(73600, () => {
      let n = 0
      confIv = setInterval(() => {
        n++
        const src = confParties[n % confParties.length]
        cf[src].tx++
        for (const d of confParties) {
          if (d === src) continue
          cf[d].rx++
          push('bs', svc.APP_FORWARD, { src: imsiOf(src), dst: imsiOf(d), kind: 'conf', bytes: '160', count: '1' })
        }
      }, 240)
      mockRef.current.intervals.push(confIv)
    })
    at(75000, pushConfStats)
    at(79000, pushConfStats)
    // ue3 hangs up: leaves the conference, 2-party continues
    at(82000, () => {
      confParties = ['ue1', 'ue2']
      push('ue3', svc.SIP_BYE_TX, { peer: imsi1 })
      push('ue3', svc.APP_CALL_END, { dst: imsi1, kind: 'voice' })
      push('ue3', svc.CONF_LEAVE, { conf_id: CONF_ID, imsi: imsi3, reason: 'hangup' })
    })
    at(82150, () => fwd(imsi3, imsi1, 'voice', '48'))
    at(82300, () => { push('ue1', svc.SIP_BYE_RX, { peer: imsi3 }); push('ue1', svc.APP_CALL_PEER_END, { src: imsi3, kind: 'voice' }) })
    at(83500, pushConfStats)
    // host ends the conference: CONF_LEAVE(host) + CONF_END, last leg BYE
    at(87000, () => {
      if (confIv) clearInterval(confIv)
      push('ue1', svc.SIP_BYE_TX, { peer: imsi2 })
      push('ue1', svc.APP_CALL_END, { dst: imsi2, kind: 'voice' })
      push('ue1', svc.CONF_LEAVE, { conf_id: CONF_ID, imsi: imsi1, reason: 'host' })
      push('ue1', svc.CONF_END, { conf_id: CONF_ID, reason: 'host' })
    })
    at(87150, () => fwd(imsi1, imsi2, 'voice', '60'))
    at(87300, () => { push('ue2', svc.SIP_BYE_RX, { peer: imsi1 }); push('ue2', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'voice' }) })
    at(88500, () => push('ue2', svc.APP_MSG_TX, { dst: imsi1, text: '多方通话很流畅' }))
    at(88650, () => fwd(imsi2, imsi1, 'msg', '28'))
    at(88800, () => push('ue1', svc.APP_MSG_RX, { src: imsi2, text: '多方通话很流畅' }))

    // --- P6: RRC Inactive / fast resume — ue2+ue3 doze off, then get paged ---
    at(92000, () => {
      push('ue3', svc.RRC_INACTIVE, { c_rnti: UE_MOCK.ue3.crnti, resume_id: '813' })
      push('ue3', ev.RRC_UE_STATE, { old: 'CONNECTED', new: 'INACTIVE' })
    })
    at(92500, () => {
      push('ue2', svc.RRC_INACTIVE, { c_rnti: UE_MOCK.ue2.crnti, resume_id: '457' })
      push('ue2', ev.RRC_UE_STATE, { old: 'CONNECTED', new: 'INACTIVE' })
    })
    // incoming call to inactive ue3: page -> fast resume -> normal ring/answer
    at(95000, () => { push('ue1', svc.SIP_INVITE_TX, { dst: imsi3, kind: 'voice' }); push('ue1', svc.APP_CALL_START, { dst: imsi3, kind: 'voice' }) })
    at(95300, () => push('bs', svc.PAGE_TX, { imsi: imsi3 }))
    at(95600, () => push('ue3', svc.PAGE_RX, { imsi: imsi3 }))
    at(95800, () => push('ue3', svc.RRC_RESUME_REQUEST, { resume_id: '813' }))
    at(96200, () => {
      push('ue3', svc.RRC_RESUMED, { c_rnti: UE_MOCK.ue3.crnti, old_c_rnti: UE_MOCK.ue3.crnti })
      push('ue3', ev.RRC_UE_STATE, { old: 'INACTIVE', new: 'CONNECTED' })
    })
    at(96400, () => { push('ue3', svc.SIP_INVITE_RX, { src: imsi1, kind: 'voice' }); push('ue3', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'voice' }) })
    at(97600, () => push('ue3', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(97750, () => fwd(imsi3, imsi1, 'voice', '48'))
    at(97900, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi3 }))
    at(99000, () => push('ue3', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'voice' }))
    at(99150, () => fwd(imsi3, imsi1, 'voice', '52'))
    at(99300, () => push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi3, kind: 'voice' }))
    at(99600, () => {
      vc.tx1 = 0; vc.rx1 = 0; vc.tx2 = 0; vc.rx2 = 0; vc.loss1 = 0; vc.loss2 = 0
      let n = 0
      voiceIv = setInterval(() => {
        n++
        const from1 = n % 2 === 1
        if (from1) { vc.tx1++; vc.rx2++ } else { vc.tx2++; vc.rx1++ }
        push('bs', svc.APP_FORWARD, {
          src: from1 ? imsi1 : imsi3, dst: from1 ? imsi3 : imsi1,
          kind: 'voice', bytes: '160', count: '4',
        })
      }, 350)
      mockRef.current.intervals.push(voiceIv)
    })
    const pushWakeCallStats = () => {
      push('ue1', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi3, tx: String(vc.tx1), rx: String(vc.rx1), loss: '0', rtt_avg: '17' })
      push('ue3', svc.APP_STREAM_STATS, { kind: 'voice', peer: imsi1, tx: String(vc.tx2), rx: String(vc.rx2), loss: '0', rtt_avg: '18' })
    }
    at(100500, pushWakeCallStats)
    at(103000, pushWakeCallStats)
    at(104000, () => {
      if (voiceIv) clearInterval(voiceIv)
      push('ue3', svc.SIP_BYE_TX, { peer: imsi1 })
      push('ue3', svc.APP_CALL_END, { dst: imsi1, kind: 'voice' })
    })
    at(104150, () => fwd(imsi3, imsi1, 'voice', '48'))
    at(104300, () => { push('ue1', svc.SIP_BYE_RX, { peer: imsi3 }); push('ue1', svc.APP_CALL_PEER_END, { src: imsi3, kind: 'voice' }) })
    // wake-by-message: ue1 texts the dozing ue2, paging resumes it first
    at(106500, () => push('ue1', svc.APP_MSG_TX, { dst: imsi2, text: '醒醒，演示快结束了' }))
    at(106650, () => push('bs', svc.PAGE_TX, { imsi: imsi2 }))
    at(106900, () => push('ue2', svc.PAGE_RX, { imsi: imsi2 }))
    at(107100, () => push('ue2', svc.RRC_RESUME_REQUEST, { resume_id: '457' }))
    at(107400, () => {
      push('ue2', svc.RRC_RESUMED, { c_rnti: UE_MOCK.ue2.crnti, old_c_rnti: UE_MOCK.ue2.crnti })
      push('ue2', ev.RRC_UE_STATE, { old: 'INACTIVE', new: 'CONNECTED' })
    })
    at(107600, () => fwd(imsi1, imsi2, 'msg', '30'))
    at(107750, () => push('ue2', svc.APP_MSG_RX, { src: imsi1, text: '醒醒，演示快结束了' }))

    // --- P8: dual-BS mobility — a ue1<->ue2 voice call rides through a
    // handover BS1 -> BS2 (signal declines, meas report, HO, recovery) ---
    let onBs2 = false
    const fwdCell = (src: string, dst: string, kind: string, bytes: string) =>
      push(onBs2 ? 'bs2' : 'bs', svc.APP_FORWARD, { src, dst, kind, bytes })
    at(110000, () => { push('ue1', svc.SIP_INVITE_TX, { dst: imsi2, kind: 'voice' }); push('ue1', svc.APP_CALL_START, { dst: imsi2, kind: 'voice' }) })
    at(110150, () => fwdCell(imsi1, imsi2, 'voice', '64'))
    at(110300, () => { push('ue2', svc.SIP_INVITE_RX, { src: imsi1, kind: 'voice' }); push('ue2', svc.APP_CALL_INCOMING, { src: imsi1, kind: 'voice' }) })
    at(111500, () => push('ue2', svc.SIP_RINGING_TX, { dst: imsi1 }))
    at(111650, () => fwdCell(imsi2, imsi1, 'voice', '48'))
    at(111800, () => push('ue1', svc.SIP_RINGING_RX, { src: imsi2 }))
    at(113000, () => push('ue2', svc.SIP_CALL_ESTABLISHED, { peer: imsi1, kind: 'voice' }))
    at(113150, () => fwdCell(imsi2, imsi1, 'voice', '52'))
    at(113300, () => push('ue1', svc.SIP_CALL_ESTABLISHED, { peer: imsi2, kind: 'voice' }))
    at(113600, () => {
      vc.tx1 = 0; vc.rx1 = 0; vc.tx2 = 0; vc.rx2 = 0; vc.loss1 = 0; vc.loss2 = 0
      let n = 0
      voiceIv = setInterval(() => {
        n++
        const from1 = n % 2 === 1
        if (from1) { vc.tx1++; vc.rx2++ } else { vc.tx2++; vc.rx1++ }
        push(onBs2 ? 'bs2' : 'bs', svc.APP_FORWARD, {
          src: from1 ? imsi1 : imsi2, dst: from1 ? imsi2 : imsi1,
          kind: 'voice', bytes: '160', count: '4',
        })
      }, 350)
      mockRef.current.intervals.push(voiceIv)
    })
    at(114500, pushVoiceStats)
    // serving cell weakens: 28 -> 24 -> 19 -> 15 dB, bars visibly drop
    at(115500, () => push('ue1', svc.LINK_QUALITY, { c_rnti: UE_MOCK.ue1.crnti, rsrp: '-84', sinr: '24' }))
    at(116500, () => push('ue1', svc.LINK_QUALITY, { c_rnti: UE_MOCK.ue1.crnti, rsrp: '-88', sinr: '19' }))
    at(117500, () => push('ue1', svc.LINK_QUALITY, { c_rnti: UE_MOCK.ue1.crnti, rsrp: '-92', sinr: '15' }))
    at(118000, () => push('ue1', svc.MEAS_REPORT_TX, { serving: '1', n: '2' }))
    // handover: brief dual-link, then done on the target cell
    at(118500, () => push('bs', svc.HANDOVER_START, { imsi: imsi1, from: '1', to: '2' }))
    at(119700, () => {
      onBs2 = true
      push('bs2', svc.HANDOVER_DONE, { imsi: imsi1, from: '1', to: '2', path: 'ho' })
    })
    at(120000, () => {
      push('ue1', svc.LINK_QUALITY, { c_rnti: UE_MOCK.ue1.crnti, rsrp: '-79', sinr: '27' }) // recovered on BS2
      push('bs2', ev.BS_STATUS, { registered_ues: '1' })
    })
    at(121000, pushVoiceStats) // media never stopped: stats keep climbing
    at(123000, () => {
      if (voiceIv) clearInterval(voiceIv)
      push('ue1', svc.SIP_BYE_TX, { peer: imsi2 })
      push('ue1', svc.APP_CALL_END, { dst: imsi2, kind: 'voice' })
    })
    at(123150, () => fwdCell(imsi1, imsi2, 'voice', '48'))
    at(123300, () => { push('ue2', svc.SIP_BYE_RX, { peer: imsi1 }); push('ue2', svc.APP_CALL_PEER_END, { src: imsi1, kind: 'voice' }) })
    at(124500, () => push('ue1', svc.APP_MSG_TX, { dst: imsi2, text: '切换无感，通话没断' }))
    at(124650, () => fwdCell(imsi1, imsi2, 'msg', '30'))
    at(124800, () => push('ue2', svc.APP_MSG_RX, { src: imsi1, text: '切换无感，通话没断' }))
  }, [setMessages])

  const toggleMock = useCallback(() => {
    if (mocking) stopMock()
    else startMock()
  }, [mocking, stopMock, startMock])

  const selectNode = useCallback((n: NodeId) => {
    setSelected(n)
    setStreamFilter(n)
  }, [])

  /** Story-view "查看原始日志": jump back to the ops dashboard, node preselected. */
  const inspectNode = useCallback((n: NodeId) => {
    setSelected(n)
    setStreamFilter(n)
    setRightTab('logs')
    setView('ops')
  }, [])

  const onCommand = useCallback((target: NodeId, cmd: string) => {
    sendCommand(target, cmd)
  }, [sendCommand])

  const filteredMessages = useMemo(
    () => (streamFilter === 'all' ? messages : messages.filter((m) => nodeOf(m) === streamFilter)),
    [messages, streamFilter],
  )

  const filteredPdus = useMemo(
    () => (streamFilter === 'all' ? pdus : pdus.filter((p) => nodeOf(p.raw) === streamFilter)),
    [pdus, streamFilter],
  )

  const selUe = selected === 'ue1' ? ue1 : selected === 'ue2' ? ue2 : selected === 'ue3' ? ue3 : null
  const bsDetail = selected === 'bs2' ? bsInfo.bs2 : bsInfo.bs

  const tabStyle = (active: boolean): React.CSSProperties => ({
    padding: '6px 14px',
    fontSize: 12,
    fontWeight: active ? 700 : 500,
    color: active ? '#e5e7eb' : '#6b7280',
    background: active ? 'rgba(59,130,246,0.12)' : 'transparent',
    border: '1px solid',
    borderColor: active ? 'rgba(59,130,246,0.3)' : 'transparent',
    borderRadius: 6,
    cursor: 'pointer',
  })

  const chipStyle = (active: boolean): React.CSSProperties => ({
    padding: '4px 10px',
    fontSize: 11,
    fontWeight: active ? 700 : 500,
    fontFamily: 'monospace',
    color: active ? '#e5e7eb' : '#6b7280',
    background: active ? 'rgba(16,185,129,0.12)' : 'rgba(255,255,255,0.02)',
    border: '1px solid',
    borderColor: active ? 'rgba(16,185,129,0.35)' : 'var(--border-color)',
    borderRadius: 6,
    cursor: 'pointer',
  })

  const detailRow = (label: string, value: string) => (
    <div key={label} style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, color: 'var(--text-secondary)' }}>
      <span>{label}</span>
      <span style={{ color: 'var(--text-primary)', fontWeight: 600, fontFamily: 'monospace' }}>{value}</span>
    </div>
  )

  const pduRow = (p: PduEntry) => {
    const layerColor: Record<string, string> = { MAC: '#3b82f6', RLC: '#8b5cf6', PDCP: '#10b981', RRC: '#f59e0b', NAS: '#ef4444', APP: '#6b7280' }
    const node = nodeOf(p.raw)
    return (
      <div key={p.id} onClick={() => setSelectedPdu(p)} style={{ display: 'flex', gap: 8, padding: '4px 0', fontSize: 12, cursor: 'pointer', borderBottom: '1px solid rgba(255,255,255,0.02)', alignItems: 'center' }}>
        <span style={{ color: '#4b5563', width: 75, flexShrink: 0, fontFamily: 'monospace', fontSize: 11 }}>#{p.seq}</span>
        <span style={{ color: node ? '#6b7280' : '#4b5563', width: 30, flexShrink: 0, fontSize: 10, fontWeight: 700 }}>{node ? NODE_LABEL[node] : '-'}</span>
        <span style={{ color: layerColor[p.layer] || '#9ca3af', fontWeight: 700, width: 35 }}>{p.layer}</span>
        <span style={{ color: p.direction === 'TX' ? '#10b981' : '#3b82f6', fontWeight: 600, width: 24 }}>{p.direction}</span>
        <span style={{ color: '#e5e7eb', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{p.brief}</span>
      </div>
    )
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100vh', padding: 16, gap: 16, background: '#05070a' }}>
      <header className="glass-panel" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '16px 24px', flexShrink: 0 }}>
        <div>
          <h1 style={{ margin: 0, fontSize: 18, fontWeight: 800, letterSpacing: 0.5 }}>AetherStack LMT</h1>
          <p style={{ margin: 0, fontSize: 11, color: 'var(--text-secondary)' }}>Wireless Protocol Stack — Local Maintenance Terminal</p>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 20 }}>
          <div style={{ display: 'flex', gap: 4, background: 'rgba(255,255,255,0.02)', padding: 4, borderRadius: 8, border: '1px solid var(--border-color)' }}>
            {([['ops', '运维视图'], ['story', '演示视图']] as [ViewMode, string][]).map(([v, label]) => (
              <button
                key={v}
                onClick={() => setView(v)}
                style={{
                  padding: '6px 14px', fontSize: 12.5, fontWeight: view === v ? 800 : 500, borderRadius: 6,
                  border: '1px solid', cursor: 'pointer',
                  borderColor: view === v ? 'rgba(59,130,246,0.35)' : 'transparent',
                  background: view === v ? 'rgba(59,130,246,0.14)' : 'transparent',
                  color: view === v ? '#e5e7eb' : '#6b7280',
                }}
              >
                {label}
              </button>
            ))}
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, background: 'rgba(255,255,255,0.02)', padding: '8px 14px', borderRadius: 8, border: '1px solid var(--border-color)' }}>
            <span style={{ width: 10, height: 10, borderRadius: '50%', background: isConnected ? '#10b981' : '#ef4444' }} />
            <span style={{ fontSize: 13, fontWeight: 600, color: isConnected ? '#10b981' : '#ef4444' }}>
              {isConnected ? 'LIVE' : 'DISCONNECTED'}
            </span>
          </div>
          <button
            onClick={toggleMock}
            style={{
              padding: '8px 16px', fontSize: 13, fontWeight: 600, borderRadius: 8, border: '1px solid',
              borderColor: mocking ? '#f59e0b' : '#3b82f6', color: mocking ? '#f59e0b' : '#60a5fa',
              background: mocking ? 'rgba(245,158,11,0.1)' : 'rgba(59,130,246,0.1)', cursor: 'pointer',
            }}
          >
            {mocking ? 'Stop Mock' : 'Start Mock'}
          </button>
        </div>
      </header>

      <DemoBanner messages={messages} />

      <main style={{ display: 'flex', flexGrow: 1, gap: 16, overflow: 'hidden' }}>
        {view === 'story' ? (
          <section className="glass-panel" style={{ flexGrow: 1, overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
            <StoryView
              messages={messages}
              presence={presence}
              ueNas={{ ue1: ue1.nas, ue2: ue2.nas, ue3: ue3.nas }}
              ueImsi={ueImsi}
              calls={calls}
              bearers={bearers}
              confs={confs}
              radio={radio}
              sleep={sleep}
              auth={auth}
              mobility={mobility}
              chat={chat}
              onInspectNode={inspectNode}
              onCommand={onCommand}
            />
          </section>
        ) : (
          <>
        <section className="glass-panel" style={{ width: 400, flexShrink: 0, padding: 20, display: 'flex', flexDirection: 'column', overflowY: 'auto', gap: 16 }}>
          <div>
            <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 12, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
              NETWORK TOPOLOGY
            </h2>
            <LiveTopology
              messages={messages}
              presence={presence}
              ueNas={{ ue1: ue1.nas, ue2: ue2.nas, ue3: ue3.nas }}
              ueCrnti={{ ue1: ue1.crnti, ue2: ue2.crnti, ue3: ue3.crnti }}
              ueImsi={ueImsi}
              calls={calls}
              bearers={bearers}
              confs={confs}
              radio={radio}
              sleep={sleep}
              mobility={mobility}
              bsRegistered={bsInfo.bs.registered}
              bs2Registered={bsInfo.bs2.registered}
              selected={selected}
              onSelect={selectNode}
              onCommand={onCommand}
              onOpenPdu={setSelectedPdu}
            />
          </div>

          <CallPanel calls={calls} />

          <ChatPanel chat={chat} />

          <div style={{ ...cardStyle, borderColor: selUe?.nas === 'REGISTERED' ? '#10b981' : 'var(--border-color)' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
              <span style={{ fontWeight: 700, fontSize: 13, color: '#fff' }}>
                {selUe ? `${NODE_LABEL[selected]} Detail` : 'gNB Detail'}
              </span>
              <span style={{ fontSize: 10, fontWeight: 800, padding: '2px 6px', borderRadius: 4, textTransform: 'uppercase', background: presence[selected] === 'OFFLINE' ? 'rgba(239,68,68,0.1)' : 'rgba(16,185,129,0.1)', color: presence[selected] === 'OFFLINE' ? '#ef4444' : '#10b981' }}>
                {presence[selected]}
              </span>
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
              {selUe ? (
                <>
                  {detailRow('C-RNTI', selUe.crnti)}
                  {detailRow('IMSI', selUe.imsi)}
                  {detailRow('TMSI', selUe.tmsi)}
                  {radio[selected as UeId].sinr != null && detailRow('SINR / RSRP', `${radio[selected as UeId].sinr} dB / ${radio[selected as UeId].rsrp ?? '-'} dBm`)}
                  {radio[selected as UeId].cqi != null && detailRow('CQI', String(radio[selected as UeId].cqi))}
                  {radio[selected as UeId].mcs && detailRow('DL MCS', mcsLabel(radio[selected as UeId].mcs)!)}
                  {radio[selected as UeId].txDbm != null && detailRow('TX Power', `${radio[selected as UeId].txDbm} dBm`)}
                  {selUe.rrc === 'INACTIVE' && detailRow('RRC', '休眠中 (INACTIVE)')}
                  {detailRow('App Rx (pong)', selUe.appRx)}
                  {selUe.traffic && detailRow('Traffic TX/RX/Loss', `${selUe.traffic.tx} / ${selUe.traffic.rx} / ${selUe.traffic.loss}`)}
                  {selUe.traffic && detailRow('RTT avg (ms)', selUe.traffic.rtt)}
                </>
              ) : (
                <>
                  {detailRow('Cell ID', bsDetail.sib?.cell_id ?? '0x0001')}
                  {detailRow('PLMN / TAC', bsDetail.sib ? `${bsDetail.sib.plmn} / ${bsDetail.sib.tac}` : '-')}
                  {detailRow('FFT / CP', `${bsDetail.phy.n_fft} / ${bsDetail.phy.cp_len}`)}
                  {detailRow('Registered UEs', bsDetail.registered)}
                </>
              )}
            </div>
          </div>

          {selUe && (
            <div>
              <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 8, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
                PROTOCOL FSM — {NODE_LABEL[selected]}
              </h2>
              <FsmViewer states={{ mac: selUe.mac, rrc: selUe.rrc, nas: selUe.nas }} />
            </div>
          )}
        </section>

        <section style={{ flexGrow: 1, height: '100%', overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
          <div style={{ display: 'flex', gap: 8, padding: '8px 0', flexShrink: 0, alignItems: 'center', flexWrap: 'wrap' }}>
            <button style={tabStyle(rightTab === 'logs')} onClick={() => setRightTab('logs')}>Event Stream</button>
            <button style={tabStyle(rightTab === 'msc')} onClick={() => setRightTab('msc')}>MSC Diagram</button>
            <button style={tabStyle(rightTab === 'pdu')} onClick={() => setRightTab('pdu')}>PDU Trace {filteredPdus.length > 0 && `(${filteredPdus.length})`}</button>
            <span style={{ width: 1, height: 18, background: 'var(--border-color)', margin: '0 4px' }} />
            {(['all', 'ue1', 'ue2', 'ue3', 'bs', 'bs2'] as StreamFilter[]).map((f) => (
              <button key={f} style={chipStyle(streamFilter === f)} onClick={() => { setStreamFilter(f); if (f !== 'all') setSelected(f) }}>
                {f === 'all' ? 'All' : NODE_LABEL[f]}
              </button>
            ))}
          </div>
          <div style={{ flexGrow: 1, overflow: 'hidden' }}>
            {rightTab === 'logs' && <LogStream messages={filteredMessages} clearMessages={clearMessages} onOpenPdu={setSelectedPdu} />}
            {rightTab === 'msc' && <MscDiagram messages={filteredMessages} />}
            {rightTab === 'pdu' && (
              <div className="glass-panel" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
                <div style={{ padding: '8px 16px', borderBottom: '1px solid var(--border-color)', fontSize: 13, fontWeight: 700, color: '#9ca3af' }}>
                  PDU TRACE {filteredPdus.length > 0 && `(${filteredPdus.length} PDUs)`}
                </div>
                <div style={{ flexGrow: 1, overflowY: 'auto', padding: '8px 16px', fontFamily: 'monospace' }}>
                  {filteredPdus.length === 0 ? (
                    <div style={{ color: '#4b5563', fontStyle: 'italic', textAlign: 'center', marginTop: 40 }}>No PDU traces received</div>
                  ) : (
                    filteredPdus.map(pduRow)
                  )}
                </div>
              </div>
            )}
          </div>
        </section>
          </>
        )}
      </main>

      <PduDetail pdu={selectedPdu} onClose={() => setSelectedPdu(null)} />
    </div>
  )
}

const cardStyle: React.CSSProperties = {
  background: 'rgba(255,255,255,0.01)',
  border: '1px solid',
  borderRadius: 10,
  padding: 14,
}

export default App

import React, { useState, useCallback, useEffect, useMemo } from 'react'
import useWebSocket, { LogEvent } from './hooks/useWebSocket'
import ev from './events'
import LogStream from './components/LogStream'
import TopologyCanvas from './components/TopologyCanvas'
import FsmViewer from './components/FsmViewer'
import MscDiagram from './components/MscDiagram'
import PduDetail, { usePduStore, PduEntry } from './components/PduDetail'

const MOCK_EVENTS: Partial<LogEvent>[] = [
  { module: 'UE', level: 'INFO', event: ev.PROCESS_START, fields: { msg: 'UE starting up' } },
  { module: 'BS', level: 'INFO', event: ev.PROCESS_START, fields: { msg: 'BS active on cell 1' } },
  { module: 'BS', level: 'INFO', event: ev.PHY_CONFIG, fields: { n_fft: '64', cp_len: '16' } },
  { module: 'BS', level: 'INFO', event: ev.BS_SIB_BROADCAST_ON, fields: { period_ms: '200' } },
  { module: 'UE', level: 'INFO', event: ev.RRC_MIB_RX, fields: { sfn: '0', bw: '50' } },
  { module: 'UE', level: 'INFO', event: ev.RRC_SIB1_RX, fields: { plmn: '46001', tac: '1', cell_id: '1' } },
  { module: 'UE', level: 'INFO', event: ev.UE_ATTACH_START, fields: { imsi: '460011234567890' } },
  { module: 'UE', level: 'INFO', event: ev.MAC_RACH_MSG1, fields: { preamble: '42', tx_count: '1' } },
  { module: 'BS', level: 'INFO', event: ev.MAC_RACH_MSG2, fields: { ra_rnti: '17194', ta: '12' } },
  { module: 'UE', level: 'INFO', event: ev.RACH_SUCCESS, fields: { c_rnti: '1' } },
  { module: 'BS', level: 'INFO', event: ev.RRC_SETUP_TX, fields: { c_rnti: '1' } },
  { module: 'UE', level: 'INFO', event: ev.NAS_ATTACH_ACCEPT_RX, fields: { tmsi: '65537' } },
  { module: 'UE', level: 'INFO', event: ev.APP_RTT, fields: { seq: '1', rtt_ms: '2' } },
  { module: 'UE', level: 'INFO', event: ev.HEARTBEAT, fields: { registered: '1' } },
]

type RightTab = 'logs' | 'msc' | 'pdu'

type NodePresence = 'OFFLINE' | 'INITIALIZING' | 'RUNNING'

/** Latest state per FSM derived from *_STATE_CHANGE field payloads (D6). */
function deriveFsm(messages: LogEvent[]) {
  let mac = 'IDLE'
  let rrc = 'IDLE'
  let nas = 'DEREGISTERED'
  for (const m of messages) {
    const next = m.fields?.new || m.fields?.new_state
    if (!next) continue
    if (m.module === 'UE') {
      if (m.event === ev.MAC_STATE_CHANGE) mac = macLabel(next)
      else if (m.event === ev.RRC_UE_STATE) rrc = next
      else if (m.event === ev.NAS_STATE_CHANGE) nas = next
    }
  }
  return { mac, rrc, nas }
}

/** Wire name -> FsmViewer display name. */
function macLabel(state: string): string {
  if (state === 'WAIT_CONTENTION_RESOLVE') return 'WAIT_CR'
  return state
}

export const App: React.FC = () => {
  const { messages, isConnected, clearMessages, setMessages } = useWebSocket('ws://localhost:8765')
  const [mocking, setMocking] = useState(false)
  const [mockTimer, setMockTimer] = useState<ReturnType<typeof setInterval> | null>(null)
  const [rightTab, setRightTab] = useState<RightTab>('logs')
  const { pdus, selectedPdu, setSelectedPdu, addEvents } = usePduStore()

  // Feed the PDU store only genuinely new events (server-side _seq).
  useEffect(() => {
    addEvents(messages)
  }, [messages, addEvents])

  const presence = useMemo((): Record<'UE' | 'BS', NodePresence> => {
    const result: Record<'UE' | 'BS', NodePresence> = { UE: 'OFFLINE', BS: 'OFFLINE' }
    for (const m of messages) {
      if (m.module !== 'UE' && m.module !== 'BS') continue
      if (m.event === ev.PROCESS_EXIT) result[m.module as 'UE' | 'BS'] = 'OFFLINE'
      else if (m.event === ev.PROCESS_START) result[m.module as 'UE' | 'BS'] = 'RUNNING'
    }
    return result
  }, [messages])

  // Telemetry cards take real values from protocol events (D6).
  const telemetry = useMemo(() => {
    let phy = { n_fft: '-', cp_len: '-' }
    let sib: { plmn: string; tac: string; cell_id: string } | null = null
    let crnti = '-'
    let appRx = '-'
    for (const m of messages) {
      if (m.event === ev.PHY_CONFIG && m.fields.n_fft) phy = { n_fft: m.fields.n_fft, cp_len: m.fields.cp_len ?? '-' }
      else if (m.event === ev.RRC_SIB1_RX) sib = { plmn: m.fields.plmn ?? '-', tac: m.fields.tac ?? '-', cell_id: m.fields.cell_id ?? '-' }
      else if (m.event === ev.UE_STATUS) {
        if (m.fields.c_rnti && m.fields.c_rnti !== '0') crnti = m.fields.c_rnti
        if (m.fields.app_rx) appRx = m.fields.app_rx
      } else if (m.event === ev.RACH_SUCCESS && m.fields.c_rnti) {
        if (crnti === '-') crnti = m.fields.c_rnti
      }
    }
    return { phy, sib, crnti, appRx }
  }, [messages])

  const fsm = deriveFsm(messages)

  const linkState = useMemo<'idle' | 'active'>(() => {
    return fsm.nas === 'REGISTERED' ? 'active' : 'idle'
  }, [fsm])

  const toggleMock = useCallback(() => {
    if (mocking) {
      if (mockTimer) clearInterval(mockTimer)
      setMockTimer(null)
      setMocking(false)
      return
    }

    setMocking(true)
    let idx = 0
    let seq = Date.now() % 100000 // mock _seq stream so the PDU store works offline
    const id = setInterval(() => {
      const raw = MOCK_EVENTS[idx % MOCK_EVENTS.length]
      const evt: LogEvent = {
        timestamp: new Date().toISOString(),
        module: raw.module || 'SYS',
        level: raw.level || 'INFO',
        event: raw.event || 'EVENT',
        fields: raw.fields || {},
        _seq: ++seq,
      }
      setMessages((prev) => [...prev, evt].slice(-200))
      idx++
    }, 1200)
    setMockTimer(id)
  }, [mocking, mockTimer, setMessages])

  const ueState = presence.UE
  const bsState = presence.BS

  const nodeCard = (title: string, state: string, details: { label: string; value: string }[]) => (
    <div style={{ ...cardStyle, borderColor: fsm.nas === 'REGISTERED' && title.includes('UE') ? '#10b981' : 'var(--border-color)' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
        <span style={{ fontWeight: 700, fontSize: 13, color: '#fff' }}>{title}</span>
        <span style={{ fontSize: 10, fontWeight: 800, padding: '2px 6px', borderRadius: 4, textTransform: 'uppercase', background: state === 'OFFLINE' ? 'rgba(239,68,68,0.1)' : 'rgba(16,185,129,0.1)', color: state === 'OFFLINE' ? '#ef4444' : '#10b981' }}>
          {state}
        </span>
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {details.map((d) => (
          <div key={d.label} style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, color: 'var(--text-secondary)' }}>
            <span>{d.label}</span>
            <span style={{ color: 'var(--text-primary)', fontWeight: 600, fontFamily: 'monospace' }}>{d.value}</span>
          </div>
        ))}
      </div>
    </div>
  )

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

  const pduRow = (p: PduEntry) => {
    const layerColor: Record<string, string> = { MAC: '#3b82f6', RLC: '#8b5cf6', PDCP: '#10b981', RRC: '#f59e0b', NAS: '#ef4444', APP: '#6b7280' }
    return (
      <div key={p.id} onClick={() => setSelectedPdu(p)} style={{ display: 'flex', gap: 8, padding: '4px 0', fontSize: 12, cursor: 'pointer', borderBottom: '1px solid rgba(255,255,255,0.02)', alignItems: 'center' }}>
        <span style={{ color: '#4b5563', width: 75, flexShrink: 0, fontFamily: 'monospace', fontSize: 11 }}>#{p.seq}</span>
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

      <main style={{ display: 'flex', flexGrow: 1, gap: 16, overflow: 'hidden' }}>
        <section className="glass-panel" style={{ width: 320, flexShrink: 0, padding: 20, display: 'flex', flexDirection: 'column', overflowY: 'auto', gap: 16 }}>
          <div>
            <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 12, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
              NETWORK TOPOLOGY
            </h2>
            <TopologyCanvas ueState={ueState} bsState={bsState} linkState={linkState} />
          </div>

          <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
            {nodeCard('User Equipment (UE)', ueState, [
              { label: 'C-RNTI', value: telemetry.crnti },
              { label: 'App Rx (pong)', value: telemetry.appRx },
              { label: 'Modulation', value: 'QPSK (AWGN)' },
            ])}
            {nodeCard('Base Station (gNB)', bsState, [
              { label: 'Cell ID', value: telemetry.sib?.cell_id ?? '0x0001' },
              { label: 'PLMN / TAC', value: telemetry.sib ? `${telemetry.sib.plmn} / ${telemetry.sib.tac}` : '-' },
              { label: 'FFT / CP', value: `${telemetry.phy.n_fft} / ${telemetry.phy.cp_len}` },
            ])}
          </div>

          <div>
            <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 8, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
              PROTOCOL FSM
            </h2>
            <FsmViewer states={{ mac: fsm.mac, rrc: fsm.rrc, nas: fsm.nas }} />
          </div>
        </section>

        <section style={{ flexGrow: 1, height: '100%', overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
          <div style={{ display: 'flex', gap: 8, padding: '8px 0', flexShrink: 0 }}>
            <button style={tabStyle(rightTab === 'logs')} onClick={() => setRightTab('logs')}>Event Stream</button>
            <button style={tabStyle(rightTab === 'msc')} onClick={() => setRightTab('msc')}>MSC Diagram</button>
            <button style={tabStyle(rightTab === 'pdu')} onClick={() => setRightTab('pdu')}>PDU Trace {pdus.length > 0 && `(${pdus.length})`}</button>
          </div>
          <div style={{ flexGrow: 1, overflow: 'hidden' }}>
            {rightTab === 'logs' && <LogStream messages={messages} clearMessages={clearMessages} />}
            {rightTab === 'msc' && <MscDiagram messages={messages} />}
            {rightTab === 'pdu' && (
              <div className="glass-panel" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
                <div style={{ padding: '8px 16px', borderBottom: '1px solid var(--border-color)', fontSize: 13, fontWeight: 700, color: '#9ca3af' }}>
                  PDU TRACE {pdus.length > 0 && `(${pdus.length} PDUs)`}
                </div>
                <div style={{ flexGrow: 1, overflowY: 'auto', padding: '8px 16px', fontFamily: 'monospace' }}>
                  {pdus.length === 0 ? (
                    <div style={{ color: '#4b5563', fontStyle: 'italic', textAlign: 'center', marginTop: 40 }}>No PDU traces received</div>
                  ) : (
                    pdus.map(pduRow)
                  )}
                </div>
              </div>
            )}
          </div>
        </section>
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

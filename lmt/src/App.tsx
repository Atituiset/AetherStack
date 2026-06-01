import React, { useState, useCallback, useEffect } from 'react'
import useWebSocket, { LogEvent } from './hooks/useWebSocket'
import LogStream from './components/LogStream'
import TopologyCanvas from './components/TopologyCanvas'
import FsmViewer from './components/FsmViewer'
import MscDiagram from './components/MscDiagram'
import PduDetail, { usePduStore, PduEntry } from './components/PduDetail'

const MOCK_EVENTS: Partial<LogEvent>[] = [
  { module: 'UE', level: 'INFO', event: 'PROCESS_START', fields: { msg: 'UE starting up' } },
  { module: 'BS', level: 'INFO', event: 'PROCESS_START', fields: { msg: 'BS active on cell 1' } },
  { module: 'UE', level: 'INFO', event: 'PHY_SYNC_REQ', fields: { freq: '3500MHz' } },
  { module: 'BS', level: 'INFO', event: 'PHY_SYNC_DETECT', fields: { snr: '18.4dB' } },
  { module: 'UE', level: 'INFO', event: 'MAC_RACH_MSG1', fields: { preamble: '42' } },
  { module: 'BS', level: 'INFO', event: 'MAC_RACH_MSG2', fields: { rar: '0x43FA' } },
  { module: 'UE', level: 'INFO', event: 'MAC_RACH_MSG3', fields: { crnti: '0x43FA' } },
  { module: 'BS', level: 'INFO', event: 'MAC_RACH_MSG4', fields: { status: 'SUCCESS' } },
  { module: 'UE', level: 'INFO', event: 'RRC_CONNECTED', fields: {} },
  { module: 'UE', level: 'INFO', event: 'NAS_ATTACH_REQ', fields: { imsi: '460011234567890' } },
  { module: 'BS', level: 'INFO', event: 'NAS_ATTACH_ACCEPT', fields: { tmsi: '0xC2841E3B' } },
  { module: 'UE', level: 'INFO', event: 'HEARTBEAT', fields: { count: '1' } },
  { module: 'BS', level: 'INFO', event: 'HEARTBEAT', fields: { ues: '1' } },
]

type RightTab = 'logs' | 'msc' | 'pdu'

export const App: React.FC = () => {
  const { messages, isConnected, clearMessages, setMessages } = useWebSocket('ws://localhost:8765')
  const [mocking, setMocking] = useState(false)
  const [mockTimer, setMockTimer] = useState<ReturnType<typeof setInterval> | null>(null)
  const [rightTab, setRightTab] = useState<RightTab>('logs')
  const { pdus, selectedPdu, setSelectedPdu, addPdu } = usePduStore()

  useEffect(() => {
    messages.forEach(addPdu)
  }, [messages, addPdu])

  const getState = (module: 'UE' | 'BS') => {
    const logs = messages.filter((m) => m.module === module)
    for (let i = logs.length - 1; i >= 0; i--) {
      const e = logs[i].event
      if (e === 'PROCESS_EXIT') return 'OFFLINE'
      if (e === 'HEARTBEAT') return 'RUNNING'
      if (e === 'NAS_ATTACH_ACCEPT' || e === 'RRC_CONNECTED') return 'REGISTERED'
      if (e === 'PROCESS_START') return 'INITIALIZING'
    }
    return 'OFFLINE'
  }

  const getMacState = () => {
    const logs = messages.filter((m) => m.module === 'UE')
    for (let i = logs.length - 1; i >= 0; i--) {
      const e = logs[i].event
      if (e === 'MAC_RACH_MSG4') return 'CONNECTED'
      if (e === 'MAC_RACH_MSG3') return 'WAIT_CR'
      if (e === 'MAC_RACH_MSG2') return 'WAIT_RAR'
      if (e === 'MAC_RACH_MSG1') return 'WAIT_RAR'
    }
    return 'IDLE'
  }

  const getRrcState = () => {
    const logs = messages.filter((m) => m.module === 'UE')
    for (let i = logs.length - 1; i >= 0; i--) {
      const e = logs[i].event
      if (e === 'RRC_CONNECTED' || e === 'NAS_ATTACH_ACCEPT') return 'CONNECTED'
      if (e === 'MAC_RACH_MSG3') return 'CONNECTING'
    }
    return 'IDLE'
  }

  const getNasState = () => {
    const logs = messages.filter((m) => m.module === 'UE')
    for (let i = logs.length - 1; i >= 0; i--) {
      const e = logs[i].event
      if (e === 'NAS_ATTACH_ACCEPT') return 'REGISTERED'
      if (e === 'NAS_ATTACH_REQ') return 'REGISTERING'
    }
    return 'DEREGISTERED'
  }

  const getLinkState = (): 'idle' | 'active' | 'error' => {
    const ue = getState('UE')
    const bs = getState('BS')
    if (ue === 'OFFLINE' || bs === 'OFFLINE') return 'idle'
    if (ue === 'REGISTERED' && bs === 'RUNNING') return 'active'
    if (ue === 'INITIALIZING' || bs === 'INITIALIZING') return 'idle'
    return 'idle'
  }

  const toggleMock = useCallback(() => {
    if (mocking) {
      if (mockTimer) clearInterval(mockTimer)
      setMockTimer(null)
      setMocking(false)
      return
    }

    setMocking(true)
    let idx = 0
    const id = setInterval(() => {
      const raw = MOCK_EVENTS[idx % MOCK_EVENTS.length]
      const evt: LogEvent = {
        timestamp: new Date().toISOString(),
        module: raw.module || 'SYS',
        level: raw.level || 'INFO',
        event: raw.event || 'EVENT',
        fields: raw.fields || {},
      }
      setMessages((prev) => [...prev, evt].slice(-200))
      idx++
    }, 1200)
    setMockTimer(id)
  }, [mocking, mockTimer, setMessages])

  const ueState = getState('UE')
  const bsState = getState('BS')
  const linkState = getLinkState()

  const nodeCard = (title: string, state: string, details: { label: string; value: string }[]) => (
    <div style={{ ...cardStyle, borderColor: state === 'REGISTERED' || state === 'RUNNING' ? (title.includes('UE') ? '#10b981' : '#3b82f6') : 'var(--border-color)' }}>
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
        <span style={{ color: '#4b5563', width: 75, flexShrink: 0, fontFamily: 'monospace', fontSize: 11 }}>{p.timestamp.split('T')[1]?.replace('Z', '')}</span>
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
              { label: 'Modulation', value: 'QPSK (AWGN)' },
              { label: 'Tx Power', value: '23 dBm' },
              { label: 'Access', value: 'SISO 5G-NR MVP' },
            ])}
            {nodeCard('Base Station (gNB)', bsState, [
              { label: 'Cell ID', value: '0x0001' },
              { label: 'Bandwidth', value: '20 MHz' },
              { label: 'Antennas', value: '1T1R (SISO)' },
            ])}
          </div>

          <div>
            <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 8, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
              PROTOCOL FSM
            </h2>
            <FsmViewer states={{ mac: getMacState(), rrc: getRrcState(), nas: getNasState() }} />
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

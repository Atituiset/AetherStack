import React, { useState, useCallback } from 'react'
import useWebSocket, { LogEvent } from './hooks/useWebSocket'
import LogStream from './components/LogStream'

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

export const App: React.FC = () => {
  const { messages, isConnected, clearMessages, setMessages } = useWebSocket('ws://localhost:8765')
  const [mocking, setMocking] = useState(false)
  const [mockTimer, setMockTimer] = useState<NodeJS.Timeout | null>(null)

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

  const nodeCard = (title: string, state: string, iconColor: string, details: { label: string; value: string }[]) => (
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

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100vh', padding: 16, gap: 16, background: '#05070a' }}>
      {/* Header */}
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
              background: mocking ? 'rgba(245,158,11,0.1)' : 'rgba(59,130,246,0.1)',
              cursor: 'pointer',
            }}
          >
            {mocking ? 'Stop Mock' : 'Start Mock'}
          </button>
        </div>
      </header>

      {/* Main */}
      <main style={{ display: 'flex', flexGrow: 1, gap: 16, overflow: 'hidden' }}>
        {/* Left panel */}
        <section className="glass-panel" style={{ width: 320, flexShrink: 0, padding: 20, display: 'flex', flexDirection: 'column', overflowY: 'auto' }}>
          <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 16, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
            NETWORK TOPOLOGY
          </h2>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
            {nodeCard('User Equipment (UE)', ueState, '#10b981', [
              { label: 'Modulation', value: 'QPSK (AWGN)' },
              { label: 'Tx Power', value: '23 dBm' },
              { label: 'Access', value: 'SISO 5G-NR MVP' },
            ])}
            {nodeCard('Base Station (gNB)', bsState, '#3b82f6', [
              { label: 'Cell ID', value: '0x0001' },
              { label: 'Bandwidth', value: '20 MHz' },
              { label: 'Antennas', value: '1T1R (SISO)' },
            ])}
          </div>
        </section>

        {/* Right panel */}
        <section style={{ flexGrow: 1, height: '100%', overflow: 'hidden' }}>
          <LogStream messages={messages} clearMessages={clearMessages} />
        </section>
      </main>
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

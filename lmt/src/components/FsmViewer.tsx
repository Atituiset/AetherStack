import React from 'react'

interface FsmState {
  mac: string
  rrc: string
  nas: string
}

interface FsmViewerProps {
  states: FsmState
}

const MAC_STATES = ['IDLE', 'WAIT_RAR', 'WAIT_CR', 'CONNECTED']
const RRC_STATES = ['IDLE', 'CONNECTING', 'CONNECTED', 'INACTIVE']
const NAS_STATES = ['DEREGISTERED', 'REGISTERING', 'REGISTERED']

const stateColor = (current: string, candidate: string) => {
  // suspended reads as amber, everything else active green
  if (current === candidate) return candidate === 'INACTIVE' ? '#f59e0b' : '#10b981'
  return '#1f2937'
}

const FsmRow: React.FC<{ label: string; states: string[]; current: string }> = ({ label, states, current }) => (
  <div style={{ marginBottom: 12 }}>
    <div style={{ fontSize: 10, fontWeight: 800, color: '#6b7280', letterSpacing: 1, marginBottom: 4 }}>{label}</div>
    <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
      {states.map((s, i) => (
        <React.Fragment key={s}>
          {i > 0 && <div style={{ width: 12, height: 1, background: '#374151' }} />}
          <div style={{
            padding: '3px 8px',
            borderRadius: 4,
            fontSize: 10,
            fontWeight: current === s ? 800 : 500,
            background: current === s ? `${stateColor(current, s)}26` : 'rgba(31,41,55,0.5)',
            color: stateColor(current, s),
            border: `1px solid ${stateColor(current, s)}22`,
            transition: 'all 0.3s ease',
          }}>
            {s}
          </div>
        </React.Fragment>
      ))}
    </div>
  </div>
)

export const FsmViewer: React.FC<FsmViewerProps> = ({ states }) => (
  <div style={{ padding: 12 }}>
    <FsmRow label="MAC RACH" states={MAC_STATES} current={states.mac} />
    <FsmRow label="RRC" states={RRC_STATES} current={states.rrc} />
    <FsmRow label="NAS" states={NAS_STATES} current={states.nas} />
  </div>
)

export default FsmViewer

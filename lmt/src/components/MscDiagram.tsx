import React, { useRef } from 'react'
import { LogEvent } from '../hooks/useWebSocket'

interface MscDiagramProps {
  messages: LogEvent[]
}

const EVENT_MSG_MAP: Record<string, { from: string; to: string; label: string }> = {
  MAC_RACH_MSG1: { from: 'UE', to: 'BS', label: 'MSG1: PRACH' },
  MAC_RACH_MSG2: { from: 'BS', to: 'UE', label: 'MSG2: RAR' },
  MAC_RACH_MSG3: { from: 'UE', to: 'BS', label: 'MSG3: RRC Req' },
  MAC_RACH_MSG4: { from: 'BS', to: 'UE', label: 'MSG4: CR' },
  RRC_SETUP_REQUEST_TX: { from: 'UE', to: 'BS', label: 'RRC Setup Req' },
  RRC_SETUP_TX: { from: 'BS', to: 'UE', label: 'RRC Setup' },
  RRC_SETUP_COMPLETE_TX: { from: 'UE', to: 'BS', label: 'RRC Setup Cmpl' },
  NAS_ATTACH_REQUEST: { from: 'UE', to: 'BS', label: 'NAS Attach Req' },
  NAS_ATTACH_ACCEPT_TX: { from: 'BS', to: 'UE', label: 'NAS Attach Accept' },
}

const MAX_MESSAGES = 50

export const MscDiagram: React.FC<MscDiagramProps> = ({ messages }) => {
  const containerRef = useRef<HTMLDivElement>(null)

  const mscMessages = messages
    .filter((m) => EVENT_MSG_MAP[m.event])
    .slice(-MAX_MESSAGES)

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', overflow: 'hidden' }}>
      <div style={{ padding: '8px 16px', borderBottom: '1px solid var(--border-color)', fontSize: 13, fontWeight: 700, color: '#9ca3af' }}>
        MSC DIAGRAM {mscMessages.length > 0 && `(${mscMessages.length} messages)`}
      </div>
      <div ref={containerRef} style={{ flexGrow: 1, overflowY: 'auto', padding: 16, fontFamily: 'monospace', fontSize: 12 }}>
        {mscMessages.length === 0 ? (
          <div style={{ color: '#4b5563', fontStyle: 'italic', textAlign: 'center', marginTop: 40 }}>No MSC messages yet</div>
        ) : (
          mscMessages.map((msg, i) => {
            const info = EVENT_MSG_MAP[msg.event]
            const isRight = info.from === 'UE'
            const time = msg.timestamp ? msg.timestamp.split('T')[1]?.replace('Z', '') : ''
            return (
              <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '4px 0', borderBottom: '1px solid rgba(255,255,255,0.02)' }}>
                <span style={{ color: '#4b5563', width: 80, flexShrink: 0 }}>{time}</span>
                <span style={{ color: isRight ? '#34d399' : '#60a5fa', width: 20, textAlign: 'center' }}>{isRight ? '→' : '←'}</span>
                <span style={{ color: '#e5e7eb', fontWeight: 600 }}>{info.label}</span>
              </div>
            )
          })
        )}
      </div>
    </div>
  )
}

export default MscDiagram

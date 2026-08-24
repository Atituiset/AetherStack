import React, { useRef } from 'react'
import ev, { MSC_UPLINK, MSC_DOWNLINK } from '../events'
import { LogEvent } from '../hooks/useWebSocket'

interface MscDiagramProps {
  messages: LogEvent[]
}

const MAX_MESSAGES = 50

export const MscDiagram: React.FC<MscDiagramProps> = ({ messages }) => {
  const containerRef = useRef<HTMLDivElement>(null)

  const mscMessages = messages
    .filter((m) => MSC_UPLINK[m.event as keyof typeof MSC_UPLINK] || MSC_DOWNLINK[m.event as keyof typeof MSC_DOWNLINK])
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
            const label =
              MSC_UPLINK[msg.event as keyof typeof MSC_UPLINK] ||
              MSC_DOWNLINK[msg.event as keyof typeof MSC_DOWNLINK] ||
              msg.event
            const isUplink = !!MSC_UPLINK[msg.event as keyof typeof MSC_UPLINK]
            const time = msg.timestamp ? msg.timestamp.split('T')[1]?.replace('Z', '') : ''
            const crnti = msg.fields?.c_rnti ? ` [${msg.fields.c_rnti}]` : ''
            return (
              <div key={`${msg._seq ?? i}-${msg.event}`} style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '4px 0', borderBottom: '1px solid rgba(255,255,255,0.02)' }}>
                <span style={{ color: '#4b5563', width: 80, flexShrink: 0 }}>{time}</span>
                <span style={{ color: isUplink ? '#34d399' : '#60a5fa', width: 20, textAlign: 'center' }}>{isUplink ? '→' : '←'}</span>
                <span style={{ color: '#e5e7eb', fontWeight: 600 }}>{label}{crnti}</span>
                <span style={{ color: '#374151', marginLeft: 'auto', fontSize: 10 }}>{msg.module}</span>
              </div>
            )
          })
        )}
      </div>
    </div>
  )
}

// keep the constant import referenced for consumers wanting raw lookup
void ev

export default MscDiagram

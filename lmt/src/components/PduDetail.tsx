import React, { useState, useCallback, useRef } from 'react'
import ev from '../events'
import { LogEvent } from '../hooks/useWebSocket'

export interface PduEntry {
  id: string
  seq: number
  timestamp: string
  direction: string
  layer: string
  hex: string
  brief: string
  raw: LogEvent
}

interface PduDetailProps {
  pdu: PduEntry | null
  onClose: () => void
}

const LAYER_COLORS: Record<string, string> = {
  MAC: '#3b82f6',
  RLC: '#8b5cf6',
  PDCP: '#10b981',
  RRC: '#f59e0b',
  NAS: '#ef4444',
  APP: '#6b7280',
}

const HexRow: React.FC<{ offset: number; bytes: number[]; layer: string }> = ({ offset, bytes, layer }) => (
  <div style={{ display: 'flex', gap: 8, fontSize: 11, lineHeight: 1.8, fontFamily: 'monospace' }}>
    <span style={{ color: '#4b5563', width: 36 }}>{offset.toString(16).padStart(4, '0')}</span>
    <span style={{ color: LAYER_COLORS[layer] || '#9ca3af' }}>
      {bytes.map((b) => b.toString(16).padStart(2, '0')).join(' ')}
    </span>
    <span style={{ color: '#6b7280' }}>
      {bytes.map((b) => (b >= 32 && b <= 126 ? String.fromCharCode(b) : '.')).join('')}
    </span>
  </div>
)

export const PduDetail: React.FC<PduDetailProps> = ({ pdu, onClose }) => {
  if (!pdu) return null

  const hexBytes = pdu.hex.replace(/:/g, '').match(/.{1,2}/g)?.map((h) => parseInt(h, 16)) || []

  return (
    <div style={{ position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.7)', display: 'flex', justifyContent: 'center', alignItems: 'center', zIndex: 1000 }} onClick={onClose}>
      <div style={{ background: '#0a0f18', border: '1px solid var(--border-color)', borderRadius: 12, padding: 24, maxWidth: 600, width: '90%', maxHeight: '80vh', overflowY: 'auto' }} onClick={(e) => e.stopPropagation()}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
          <div>
            <span style={{ color: LAYER_COLORS[pdu.layer] || '#9ca3af', fontWeight: 800, fontSize: 14 }}>{pdu.layer}</span>
            <span style={{ color: '#6b7280', marginLeft: 8, fontSize: 12 }}>[{pdu.direction}] {pdu.brief}</span>
          </div>
          <button onClick={onClose} style={{ background: '#1f2937', color: '#f3f4f6', border: '1px solid rgba(255,255,255,0.1)', borderRadius: 6, padding: '4px 12px', cursor: 'pointer', fontSize: 12 }}>Close</button>
        </div>
        <div style={{ borderTop: '1px solid var(--border-color)', paddingTop: 12 }}>
          {Array.from({ length: Math.ceil(hexBytes.length / 16) }, (_, i) => (
            <HexRow key={i} offset={i * 16} bytes={hexBytes.slice(i * 16, i * 16 + 16)} layer={pdu.layer} />
          ))}
        </div>
        <div style={{ marginTop: 12, fontSize: 11, color: '#4b5563' }}>
          #{pdu.seq} · {pdu.timestamp}
        </div>
      </div>
    </div>
  )
}

/**
 * PDU store fed incrementally by server-side `_seq` (M6.5 D6): each event is
 * processed exactly once even though the message window slides.
 */
export function usePduStore() {
  const [pdus, setPdus] = useState<PduEntry[]>([])
  const [selectedPdu, setSelectedPdu] = useState<PduEntry | null>(null)
  const lastSeqRef = useRef(0)

  const addEvents = useCallback((events: LogEvent[]) => {
    const fresh = events.filter(
      (e) => e.event === ev.PDU_TRACE && typeof e._seq === 'number' && e._seq > lastSeqRef.current,
    )
    if (fresh.length === 0) return
    lastSeqRef.current = Math.max(...fresh.map((e) => e._seq!), lastSeqRef.current)
    setPdus((prev) =>
      [
        ...prev,
        ...fresh.map((e) => ({
          id: `${e._seq}`,
          seq: e._seq!,
          timestamp: e.timestamp,
          direction: e.fields.direction || '?',
          layer: e.fields.layer || '?',
          hex: e.fields.hex || '',
          brief: e.fields.brief || '',
          raw: e,
        })),
      ].slice(-200),
    )
  }, [])

  return { pdus, selectedPdu, setSelectedPdu, addEvents }
}

export default PduDetail

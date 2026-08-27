import React, { useEffect, useRef, useState } from 'react'
import { LogEvent } from '../hooks/useWebSocket'
import { nodeOf, NODE_LABEL, NODE_COLOR } from '../nodes'
import ev from '../events'
import { PduEntry, pduFromEvent } from './PduDetail'

interface LogStreamProps {
  messages: LogEvent[]
  clearMessages: () => void
  onOpenPdu?: (p: PduEntry) => void
}

export const LogStream: React.FC<LogStreamProps> = ({ messages, clearMessages, onOpenPdu }) => {
  const [filterNode, setFilterNode] = useState<'ALL' | 'ue1' | 'ue2' | 'ue3' | 'bs'>('ALL')
  const [filterLevel, setFilterLevel] = useState<'ALL' | 'DEBUG' | 'INFO' | 'WARN' | 'ERROR'>('ALL')
  const [searchTerm, setSearchTerm] = useState('')
  const [autoScroll, setAutoScroll] = useState(true)

  const endRef = useRef<HTMLDivElement>(null)

  const filtered = messages.filter((msg) => {
    if (filterNode !== 'ALL' && nodeOf(msg) !== filterNode) return false
    if (filterLevel !== 'ALL' && msg.level !== filterLevel) return false
    if (searchTerm) {
      const term = searchTerm.toLowerCase()
      return (
        msg.event.toLowerCase().includes(term) ||
        JSON.stringify(msg.fields).toLowerCase().includes(term)
      )
    }
    return true
  })

  useEffect(() => {
    if (autoScroll && endRef.current) {
      endRef.current.scrollIntoView({ behavior: 'smooth' })
    }
  }, [filtered, autoScroll])

  const levelColor = (lvl: string): React.CSSProperties => {
    switch (lvl) {
      case 'DEBUG': return { color: '#6b7280' }
      case 'INFO':  return { color: '#10b981' }
      case 'WARN':  return { color: '#f59e0b' }
      case 'ERROR': return { color: '#ef4444' }
      default:      return { color: '#9ca3af' }
    }
  }

  const nodeBadge = (msg: LogEvent): React.CSSProperties => {
    const node = nodeOf(msg)
    if (node) {
      const c = NODE_COLOR[node]
      return { background: `${c}22`, color: c }
    }
    return { background: 'rgba(107,114,128,0.15)', color: '#9ca3af' }
  }

  const nodeTag = (msg: LogEvent): string => {
    const node = nodeOf(msg)
    return node ? NODE_LABEL[node] : msg.module || 'SYS'
  }

  return (
    <div className="glass-panel" style={{ display: 'flex', flexDirection: 'column', height: '100%', overflow: 'hidden' }}>
      {/* Header */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '12px 16px', borderBottom: '1px solid var(--border-color)', background: '#090d14' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <span style={{ width: 12, height: 12, borderRadius: '50%', background: '#ef4444' }} />
          <span style={{ width: 12, height: 12, borderRadius: '50%', background: '#f59e0b' }} />
          <span style={{ width: 12, height: 12, borderRadius: '50%', background: '#10b981' }} />
          <span style={{ marginLeft: 8, fontSize: 14, fontWeight: 600 }}>EVENT STREAM</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
          <label style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 12, color: 'var(--text-secondary)', cursor: 'pointer' }}>
            <input type="checkbox" checked={autoScroll} onChange={(e) => setAutoScroll(e.target.checked)} />
            Auto-Scroll
          </label>
          <button onClick={clearMessages} style={{ padding: '4px 10px', fontSize: 12, background: '#1f2937', color: '#f3f4f6', border: '1px solid rgba(255,255,255,0.1)', borderRadius: 6, cursor: 'pointer' }}>
            Clear
          </button>
        </div>
      </div>

      {/* Filters */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '8px 16px', borderBottom: '1px solid var(--border-color)', background: '#0a0f18', gap: 12 }}>
        <div style={{ display: 'flex', gap: 8 }}>
          <select value={filterNode} onChange={(e) => setFilterNode(e.target.value as any)} style={selectStyle}>
            <option value="ALL">All Nodes</option>
            <option value="ue1">UE1 Only</option>
            <option value="ue2">UE2 Only</option>
            <option value="ue3">UE3 Only</option>
            <option value="bs">gNB Only</option>
          </select>
          <select value={filterLevel} onChange={(e) => setFilterLevel(e.target.value as any)} style={selectStyle}>
            <option value="ALL">All Levels</option>
            <option value="DEBUG">DEBUG</option>
            <option value="INFO">INFO</option>
            <option value="WARN">WARN</option>
            <option value="ERROR">ERROR</option>
          </select>
        </div>
        <input
          type="text"
          placeholder="Search event or fields..."
          value={searchTerm}
          onChange={(e) => setSearchTerm(e.target.value)}
          style={{ ...selectStyle, flexGrow: 1, maxWidth: 300 }}
        />
      </div>

      {/* Log lines */}
      <div style={{ flexGrow: 1, overflowY: 'auto', padding: 16, fontFamily: 'monospace', fontSize: 13, lineHeight: 1.6, background: '#05070a' }}>
        {filtered.length === 0 ? (
          <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', height: '100%', color: 'var(--text-muted)', fontStyle: 'italic' }}>
            No events. Waiting for transmissions...
          </div>
        ) : (
          filtered.map((msg, i) => {
            const time = msg.timestamp ? msg.timestamp.split('T')[1]?.replace('Z', '') : ''
            const isPdu = msg.event === ev.PDU_TRACE
            return (
              <div
                key={msg._seq ?? i}
                onClick={() => isPdu && onOpenPdu?.(pduFromEvent(msg))}
                title={isPdu ? 'Open PDU detail' : undefined}
                style={{ display: 'flex', gap: 10, padding: '3px 0', borderBottom: '1px solid rgba(255,255,255,0.02)', alignItems: 'flex-start', cursor: isPdu ? 'pointer' : 'default' }}
              >
                <span style={{ color: 'var(--text-muted)', whiteSpace: 'nowrap' }}>{time}</span>
                <span style={{ padding: '1px 6px', borderRadius: 4, fontWeight: 'bold', fontSize: 11, width: 40, textAlign: 'center', whiteSpace: 'nowrap', ...nodeBadge(msg) }}>
                  {nodeTag(msg)}
                </span>
                <span style={{ width: 55, fontWeight: 600, ...levelColor(msg.level) }}>{msg.level}</span>
                <span style={{ color: '#e5e7eb', fontWeight: 'bold', minWidth: 150 }}>{msg.event}</span>
                <span style={{ color: '#a7f3d0', wordBreak: 'break-all' }}>{JSON.stringify(msg.fields)}</span>
              </div>
            )
          })
        )}
        <div ref={endRef} />
      </div>
    </div>
  )
}

const selectStyle: React.CSSProperties = {
  background: '#111827',
  color: '#f3f4f6',
  border: '1px solid rgba(255,255,255,0.1)',
  borderRadius: 6,
  padding: '4px 8px',
  fontSize: 12,
  cursor: 'pointer',
}

export default LogStream

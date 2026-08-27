import React, { useEffect, useRef } from 'react'
import { ChatMessage } from '../services'
import { NODE_COLOR } from '../nodes'

interface ChatPanelProps {
  chat: ChatMessage[]
}

/** Compact chat view of UE-to-UE text messages (APP_MSG_TX / APP_MSG_RX). */
export const ChatPanel: React.FC<ChatPanelProps> = ({ chat }) => {
  const endRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [chat.length])

  if (chat.length === 0) return null

  return (
    <div>
      <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 8, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
        消息记录
      </h2>
      <div
        className="glass-panel"
        style={{ padding: 12, display: 'flex', flexDirection: 'column', gap: 8, maxHeight: 220, overflowY: 'auto' }}
      >
        {chat.map((c) => {
          const right = c.from !== 'ue1' // UE1 bubbles left, others right
          const color = c.from ? NODE_COLOR[c.from] : '#9ca3af'
          return (
            <div key={c.id} style={{ alignSelf: right ? 'flex-end' : 'flex-start', maxWidth: '85%', textAlign: right ? 'right' : 'left' }}>
              <div style={{ fontSize: 10, color: '#6b7280', marginBottom: 2, fontFamily: 'monospace' }}>
                {c.fromLabel} → {c.toLabel} · {c.time}
              </div>
              <div
                style={{
                  display: 'inline-block',
                  background: `${color}1a`,
                  border: `1px solid ${color}44`,
                  borderRadius: 8,
                  padding: '5px 10px',
                  fontSize: 12,
                  color: '#e5e7eb',
                  textAlign: 'left',
                  wordBreak: 'break-word',
                }}
              >
                {c.text}
              </div>
            </div>
          )
        })}
        <div ref={endRef} />
      </div>
    </div>
  )
}

export default ChatPanel

import React from 'react'
import ev from '../events'
import { LogEvent } from '../hooks/useWebSocket'

interface DemoState {
  phase: string
  title: string
  detail: string
  progress: number
}

const PHASE_COLOR: Record<string, string> = {
  boot: '#3b82f6',
  attach: '#8b5cf6',
  traffic: '#10b981',
  release: '#f59e0b',
  done: '#10b981',
}

/**
 * M8.3: demo-mode banner. Renders while DEMO_PHASE events stream in;
 * hides a few seconds after the last one so normal operation is clean.
 */
export const DemoBanner: React.FC<{ messages: LogEvent[] }> = ({ messages }) => {
  const [demo, setDemo] = React.useState<DemoState | null>(null)
  const hideTimer = React.useRef<ReturnType<typeof setTimeout> | null>(null)

  React.useEffect(() => {
    for (let i = messages.length - 1; i >= 0; --i) {
      const m = messages[i]
      if (m.event === ev.DEMO_PHASE && m.module === 'DEMO') {
        setDemo({
          phase: m.fields.phase || '',
          title: m.fields.title || '演示',
          detail: m.fields.detail || '',
          progress: parseInt(m.fields.progress || '0', 10),
        })
        if (hideTimer.current) clearTimeout(hideTimer.current)
        if (m.fields.phase === 'done') {
          hideTimer.current = setTimeout(() => setDemo(null), 8000)
        }
        return
      }
      // stop scanning once we leave the recent window of interest
      if (messages.length - i > 200) break
    }
  }, [messages])

  if (!demo) return null
  const color = PHASE_COLOR[demo.phase] || '#3b82f6'

  return (
    <div style={{
      flexShrink: 0,
      border: `1px solid ${color}55`,
      background: `${color}14`,
      borderRadius: 10,
      padding: '12px 18px',
      display: 'flex',
      alignItems: 'center',
      gap: 16,
    }}>
      <div style={{ display: 'flex', flexDirection: 'column', minWidth: 260, flexGrow: 1 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 6 }}>
          <span style={{ fontSize: 13, fontWeight: 800, color: '#fff' }}>
            🎬 演示模式 · {demo.title}
          </span>
          <span style={{ fontSize: 11, fontFamily: 'monospace', color }}>{demo.progress}%</span>
        </div>
        <div style={{ height: 6, borderRadius: 3, background: 'rgba(255,255,255,0.06)', overflow: 'hidden' }}>
          <div style={{
            height: '100%', width: `${demo.progress}%`, borderRadius: 3,
            background: `linear-gradient(90deg, ${color}88, ${color})`,
            transition: 'width 600ms ease',
          }} />
        </div>
      </div>
      <div style={{ fontSize: 12, color: 'var(--text-secondary)', maxWidth: 420 }}>
        {demo.detail}
      </div>
    </div>
  )
}

export default DemoBanner

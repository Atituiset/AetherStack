import React, { useEffect, useState } from 'react'
import { CallMap, CallState, KIND_COLOR, KIND_SHORT, QCI_INFO, shortImsi } from '../services'
import { NODE_LABEL, UE_NODES } from '../nodes'

interface CallPanelProps {
  /** Concurrent calls per UE, keyed by service kind. */
  calls: CallMap
}

const KIND_LABEL: Record<string, string> = {
  voice: '语音通话中',
  video: '视频通话中',
  conf: '多方通话中',
}

/** Pre-establishment (SIP ringing) vs in-call label. */
function callLabel(call: CallState): string {
  const k = KIND_SHORT[call.kind]
  if (call.established) return KIND_LABEL[call.kind] ?? `${k}通话中`
  return call.role === 'caller' ? `${k}呼叫中` : `${k}振铃中`
}

function fmtElapsed(startedAt: number, now: number): string {
  const s = Math.max(0, Math.floor((now - startedAt) / 1000))
  const mm = String(Math.floor(s / 60)).padStart(2, '0')
  const ss = String(s % 60).padStart(2, '0')
  return `${mm}:${ss}`
}

/** Active-call status per UE and bearer kind: pulse, kind, QCI badge, elapsed timer, stream stats. */
export const CallPanel: React.FC<CallPanelProps> = ({ calls }) => {
  // one card per (ue, kind): concurrent voice+video on one UE render separately
  const entries = UE_NODES.flatMap((id) =>
    (Object.values(calls[id] ?? {}) as CallState[]).map((call) => ({ id, call })),
  )
  const [now, setNow] = useState(() => Date.now())

  // 1 Hz ticker drives the elapsed timers only while a call is up.
  useEffect(() => {
    if (entries.length === 0) return
    const iv = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(iv)
  }, [entries.length])

  if (entries.length === 0) return null

  return (
    <div>
      <h2 style={{ fontSize: 13, fontWeight: 800, color: 'var(--text-secondary)', letterSpacing: 1.5, marginBottom: 8, borderBottom: '1px solid var(--border-color)', paddingBottom: 8 }}>
        通话状态
      </h2>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        {entries.map(({ id, call }) => {
          const color = KIND_COLOR[call.kind]
          const qci = call.stats?.qci ? QCI_INFO[call.stats.qci] : undefined
          return (
            <div
              key={`${id}:${call.kind}`}
              className="glass-panel"
              style={{ padding: '10px 12px', border: `1px solid ${color}55`, background: `${color}0d` }}
            >
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <span className="lmt-pulse" style={{ width: 9, height: 9, borderRadius: '50%', background: color, flexShrink: 0 }} />
                <span style={{ fontSize: 12, fontWeight: 800, color: '#fff' }}>{NODE_LABEL[id]}</span>
                <span style={{ fontSize: 12, fontWeight: 700, color }}>{callLabel(call)}</span>
                {qci && (
                  <span
                    style={{
                      fontSize: 9.5, fontWeight: 800, padding: '1px 5px', borderRadius: 4,
                      background: `${qci.color}1f`, color: qci.color, border: `1px solid ${qci.color}55`,
                      fontFamily: 'monospace', whiteSpace: 'nowrap',
                    }}
                  >
                    {qci.label}
                  </span>
                )}
                <span
                  style={{
                    fontSize: 10, fontWeight: 700, padding: '1px 6px', borderRadius: 4,
                    background: call.role === 'callee' ? 'rgba(245,158,11,0.15)' : 'rgba(255,255,255,0.05)',
                    color: call.role === 'callee' ? '#f59e0b' : '#9ca3af',
                  }}
                >
                  {call.role === 'callee' ? '来电' : '主叫'}
                </span>
                <span style={{ marginLeft: 'auto', fontSize: 12, fontFamily: 'monospace', color: '#e5e7eb' }}>
                  {fmtElapsed(call.startedAt, now)}
                </span>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 6, fontSize: 11, color: 'var(--text-secondary)' }}>
                {call.kind === 'conf' ? (
                  <span>BS 音频桥接</span>
                ) : (
                  <span>对端 <span style={{ fontFamily: 'monospace', color: '#e5e7eb' }}>{shortImsi(call.peer)}</span></span>
                )}
                {call.stats && (
                  <span style={{ fontFamily: 'monospace' }}>
                    TX {call.stats.tx} · RX {call.stats.rx} · 丢包 {call.stats.loss} · RTT {call.stats.rtt}ms
                  </span>
                )}
              </div>
            </div>
          )
        })}
      </div>
    </div>
  )
}

export default CallPanel

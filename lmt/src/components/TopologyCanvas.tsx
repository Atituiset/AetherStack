import React from 'react'

interface TopologyProps {
  ueState: string
  bsState: string
  linkState: 'idle' | 'active' | 'error'
}

const stateColor = (s: string) => {
  if (s === 'REGISTERED' || s === 'RUNNING') return '#10b981'
  if (s === 'INITIALIZING') return '#f59e0b'
  return '#6b7280'
}

const linkColor = (l: string) => {
  if (l === 'active') return '#10b981'
  if (l === 'error') return '#ef4444'
  return '#374151'
}

export const TopologyCanvas: React.FC<TopologyProps> = ({ ueState, bsState, linkState }) => {
  return (
    <svg viewBox="0 0 400 160" style={{ width: '100%', height: 'auto' }}>
      {/* UE Node */}
      <circle cx={80} cy={80} r={36} fill="none" stroke={stateColor(ueState)} strokeWidth={2.5} />
      <text x={80} y={74} textAnchor="middle" fill="#e5e7eb" fontSize={14} fontWeight={700}>UE</text>
      <text x={80} y={92} textAnchor="middle" fill={stateColor(ueState)} fontSize={9} fontWeight={600}>{ueState}</text>

      {/* Link line */}
      <line x1={120} y1={80} x2={280} y2={80} stroke={linkColor(linkState)} strokeWidth={2} strokeDasharray={linkState === 'idle' ? '6 4' : 'none'}>
        {linkState === 'active' && (
          <animate attributeName="stroke-opacity" values="1;0.3;1" dur="1.5s" repeatCount="indefinite" />
        )}
      </line>
      <text x={200} y={68} textAnchor="middle" fill="#6b7280" fontSize={9}>
        {linkState === 'active' ? 'LINK ACTIVE' : linkState === 'error' ? 'LINK ERROR' : 'NO LINK'}
      </text>

      {/* BS Node */}
      <rect x={296} y={44} width={72} height={72} rx={10} fill="none" stroke={stateColor(bsState)} strokeWidth={2.5} />
      <text x={332} y={74} textAnchor="middle" fill="#e5e7eb" fontSize={14} fontWeight={700}>gNB</text>
      <text x={332} y={92} textAnchor="middle" fill={stateColor(bsState)} fontSize={9} fontWeight={600}>{bsState}</text>
    </svg>
  )
}

export default TopologyCanvas

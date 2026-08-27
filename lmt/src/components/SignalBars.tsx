import React from 'react'

interface SignalBarsProps {
  /** Left edge of the bar group (SVG coords). */
  x: number
  /** Baseline: bottom edge of the bars (SVG coords). */
  y: number
  /** 0..4 filled bars. */
  bars: number
  color?: string
}

const BAR_W = 3
const BAR_GAP = 1.6
/** Total width of the 4-bar group, for layout offsets. */
export const SIGNAL_BARS_WIDTH = 4 * BAR_W + 3 * BAR_GAP

/** Tiny 4-bar signal indicator for SVG scenes (both views). */
export const SignalBars: React.FC<SignalBarsProps> = ({ x, y, bars, color = '#34d399' }) => (
  <g>
    {[0, 1, 2, 3].map((i) => {
      const h = 4 + i * 2.4
      return (
        <rect
          key={i}
          x={x + i * (BAR_W + BAR_GAP)}
          y={y - h}
          width={BAR_W}
          height={h}
          rx={0.8}
          fill={i < bars ? color : 'rgba(255,255,255,0.12)'}
        />
      )
    })}
  </g>
)

export default SignalBars

import { useCallback, useEffect, useRef, useState } from 'react'

export interface LogEvent {
  timestamp: string
  module: string
  level: string
  event: string
  fields: Record<string, string>
  /** Node identity for multi-UE streams: "ue1" | "ue2" | "bs". Absent on old streams. */
  node?: string
  /** Server-side monotonic sequence (added by log_server). */
  _seq?: number
}

/** Bounded in-memory window; large enough for per-node filtering under PDU floods. */
export const MAX_MESSAGES = 500

export function useWebSocket(url: string = 'ws://localhost:8765') {
  const [messages, setMessages] = useState<LogEvent[]>([])
  const [isConnected, setIsConnected] = useState(false)
  const wsRef = useRef<WebSocket | null>(null)
  const reconnectRef = useRef(0)

  const connect = useCallback(() => {
    if (wsRef.current) {
      wsRef.current.close()
    }

    console.log(`[LMT] Connecting to ${url}...`)
    const ws = new WebSocket(url)
    wsRef.current = ws

    ws.onopen = () => {
      console.log(`[LMT] Connected to ${url}`)
      setIsConnected(true)
      reconnectRef.current = 0
    }

    ws.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data) as LogEvent
        setMessages((prev) => {
          const next = [...prev, data]
          return next.length > MAX_MESSAGES ? next.slice(next.length - MAX_MESSAGES) : next
        })
      } catch (err) {
        console.error('[LMT] Parse error:', err)
      }
    }

    ws.onclose = () => {
      setIsConnected(false)
      if (reconnectRef.current < 5) {
        const delay = Math.min(1000 * 2 ** reconnectRef.current, 10000)
        console.log(`[LMT] Reconnecting in ${delay}ms (attempt ${reconnectRef.current + 1}/5)`)
        reconnectRef.current += 1
        setTimeout(connect, delay)
      } else {
        console.log('[LMT] Max reconnect attempts reached.')
      }
    }

    ws.onerror = (err) => {
      console.error('[LMT] WebSocket error:', err)
    }
  }, [url])

  useEffect(() => {
    connect()
    return () => {
      wsRef.current?.close()
    }
  }, [connect])

  const clearMessages = () => setMessages([])

  /**
   * Command channel (log_server ws_handler): inbound JSON frames
   * {"target": "ue1"|"ue2"|"bs", "cmd": "<line>"} are forwarded as raw UDP
   * lines to the node command ports. Silently dropped while disconnected.
   */
  const sendCommand = useCallback((target: string, cmd: string) => {
    const ws = wsRef.current
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      console.warn(`[LMT] command dropped (socket not open): ${target} ${cmd}`)
      return
    }
    ws.send(JSON.stringify({ target, cmd }))
  }, [])

  return { messages, isConnected, clearMessages, setMessages, sendCommand }
}

export default useWebSocket

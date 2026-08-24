import { useCallback, useEffect, useRef, useState } from 'react'

export interface LogEvent {
  timestamp: string
  module: string
  level: string
  event: string
  fields: Record<string, string>
  /** Server-side monotonic sequence (added by log_server). */
  _seq?: number
}

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
          return next.length > 200 ? next.slice(next.length - 200) : next
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

  return { messages, isConnected, clearMessages, setMessages }
}

export default useWebSocket

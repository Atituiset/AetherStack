# WebSocket 连接

位置: `lmt/src/hooks/useWebSocket.ts`

## 功能

- 连接到 `ws://localhost:8765` (log_server.py)
- 自动重连: 最多 5 次, 指数退避 (1s, 2s, 4s, 8s, 16s)
- 解析 JSON 日志消息, 触发组件更新

## 接口

```typescript
interface WebSocketMessage {
  timestamp: string;
  module: string;
  level: string;
  event: string;
  fields?: Record<string, string>;
}

function useWebSocket(url: string = "ws://localhost:8765"): {
  messages: WebSocketMessage[];
  connected: boolean;
};
```

## 实现要点

- 使用 `useRef` 持有 WebSocket 实例, 避免重渲染导致重连
- 计时器类型使用 `ReturnType<typeof setInterval>` 而非 `NodeJS.Timeout` (跨浏览器/Node 兼容)
- 重连逻辑在 `onclose` 中触发, `onerror` 不额外触发 (避免双重重连)
- `messages` 数组限制最大长度, 丢弃最旧消息

## 数据流

```
C++ Logger ──UDP:9999──→ log_server.py ──ws:8765──→ useWebSocket ──→ React 组件
                              │
                    解析 JSON, 广播给
                    所有 WebSocket 客户端
```

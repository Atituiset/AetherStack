# Web LMT 本地维护终端

AetherStack 的 Web LMT 是一个基于 React + TypeScript 的实时可观测性前端，通过 WebSocket 接收协议栈运行状态。

## 技术栈

| 组件 | 技术 |
|------|------|
| 框架 | React 18 + TypeScript |
| 构建 | Vite |
| 实时通信 | WebSocket (ws://) |
| 状态管理 | React hooks (useState/useRef) |
| 样式 | 内联 CSS (无 UI 库依赖) |

## 目录结构

```
lmt/
├── package.json
├── tsconfig.json
├── vite.config.ts
├── index.html
└── src/
    ├── App.tsx                  # 主布局: 左面板 + 右 Tab 面板
    ├── main.tsx
    ├── components/
    │   ├── LogStream.tsx        # 日志流查看器
    │   ├── TopologyCanvas.tsx   # SVG 拓扑图
    │   ├── FsmViewer.tsx        # 状态机可视化
    │   ├── MscDiagram.tsx       # 消息序列图
    │   └── PduDetail.tsx        # PDU 十六进制详情
    └── hooks/
        └── useWebSocket.ts      # WebSocket 连接管理
```

## 数据流

```
C++ 协议栈 → Logger(UDP:9999) → log_server.py → WebSocket(:8765) → LMT 浏览器
```

# LMT 组件

## App.tsx 布局

M6 重构后的布局:

```
┌──────────────────────────────────────────────┐
│  AetherStack LMT                             │
├────────────────┬─────────────────────────────┤
│  左面板 (40%)   │  右 Tab 面板 (60%)          │
│                │                             │
│  TopologyCanvas│  [Logs] [MSC] [PDU]         │
│  (SVG 拓扑)    │                             │
│                │  LogStream / MscDiagram /   │
│  FsmViewer     │  PduDetail                  │
│  (3 行 FSM)    │                             │
│                │                             │
└────────────────┴─────────────────────────────┘
```

---

## TopologyCanvas

SVG 渲染的网络拓扑:

- **UE**: 圆形 (蓝色), 标签 "UE"
- **BS**: 矩形 (绿色), 标签 "gNB"
- **连接线**: 虚线 + 流动动画 (SVG `stroke-dashoffset` 动画)
- **状态指示**: 根据连接状态改变颜色

---

## FsmViewer

三行 FSM 状态可视化:

| 行 | 协议层 | 状态 |
|----|--------|------|
| 1 | MAC | IDLE / WAIT_RAR / WAIT_CR / CONNECTED |
| 2 | RRC | IDLE / CONNECTING / CONNECTED |
| 3 | NAS | DEREGISTERED / REGISTERING / REGISTERED |

- 当前状态高亮 (绿色背景)
- 非当前状态灰色
- 通过 WebSocket 事件驱动状态更新

---

## MscDiagram

消息序列图 (MSC), 采用**箭头列表格式** (非 Mermaid.js 运行时渲染):

```
UE → gNB: MAC_RACH_MSG1
gNB → UE: MAC_RACH_MSG2
UE → gNB: MAC_RACH_MSG3
gNB → UE: MAC_RACH_MSG4
UE → gNB: RRC_SETUP_REQUEST_TX
gNB → UE: RRC_SETUP_TX
UE → gNB: RRC_SETUP_COMPLETE_TX
UE → gNB: NAS_ATTACH_REQUEST
gNB → UE: NAS_ATTACH_ACCEPT_TX
```

- 支持 9 种事件类型
- 滚动窗口显示最近 50 条消息
- 新消息自动追加到底部

---

## PduDetail

PDU 十六进制详情查看器:

- 点击 MSC 中的消息可打开 PDU 详情模态框
- 显示格式: 偏移 | 十六进制 | ASCII
- 按层着色:

| 层 | 颜色 |
|----|------|
| MAC | #3b82f6 (蓝) |
| RLC | #8b5cf6 (紫) |
| PDCP | #10b981 (绿) |
| RRC | #f59e0b (橙) |
| NAS | #ef4444 (红) |
| APP | #6b7280 (灰) |

- 使用 `usePduStore` 自定义 hook 管理选中 PDU 状态

---

## DemoBanner (M8.3)

演示模式横幅。从消息流读取最新 `module=DEMO / event=DEMO_PHASE`
（fields: phase/title/progress/detail），渲染渐变进度条与阶段说明；
done 后停留 8s 自动隐藏，无演示事件时零渲染。挂在 App header 与 main 之间。

事件契约见 [演示系统](../demo/demo.md)；常量定义于 `lmt/src/events.ts`
（与 C++ events.h 由 CI 脚本强制一致）。

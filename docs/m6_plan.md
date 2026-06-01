# M6 计划书：Web LMT 核心功能开发

> **状态**: `COMPLETED` — M6.1–M6.4 全部实现并通过验证。
> **预计工期**: 3 周（可与 M4/M5 部分并行）
> **前置依赖**: M0 Web LMT 骨架 + M4 控制面流程数据

---

## 目标

将 Web LMT 从"日志查看器"升级为**真正的网络监控终端**：
- 实时拓扑图（UE/BS 状态可视化）
- 状态机动画（MAC/RRC/NAS 状态实时切换）
- 消息序列图（MSC）实时渲染
- PDU 十六进制查看器

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M6.1** | 双设备拓扑图 | `web_topology.md` | Canvas/SVG 画出 UE 和 BS，连接线在 PHY 事件时闪烁 |
| **M6.2** | 实时状态机视图 | `web_fsm_viewer.md` | 收到 `MAC_STATE_CHANGE` / `RRC_STATE_CHANGE` / `NAS_ATTACH_ACCEPT` 时高亮状态 |
| **M6.3** | 消息序列图（MSC） | `web_msc.md` | 实时渲染 RACH 四步、RRC 三条、NAS 两条消息；支持滚动和缩放 |
| **M6.4** | PDU 十六进制查看器 | `web_pdu_detail.md` | 点击任意消息，弹出该消息在各层的 hex dump 及字段解析 |

---

## 接口契约

### WebSocket 事件订阅

前端订阅以下事件类型：

```typescript
type WsEvent =
  | { event: 'MAC_STATE_CHANGE'; old_state: string; new_state: string }
  | { event: 'RRC_STATE_CHANGE'; old_state: string; new_state: string }
  | { event: 'NAS_STATE_CHANGE'; state: string }
  | { event: 'PDU_TRACE'; layer: string; hex: string; brief: string }
  | { event: 'PHY_RX_OK'; snr: string }
```

### PDU 详情 API

```typescript
// 请求某次连接的完整 PDU 链
fetch('/api/pdu_chain?connection_id=xxx')
  → { mac: { hex, parsed }, rlc: { hex, parsed }, ... }
```

---

## 技术选型

| 功能 | 技术方案 | 理由 |
|------|---------|------|
| 拓扑图 | Canvas 2D（自研） | 轻量，无额外依赖 |
| 状态机 | CSS transition + React state | 简单够用 |
| MSC | Mermaid.js 动态渲染 | 已有成熟库，支持时序图 |
| PDU 查看 | 自定义组件 | 层格式自定义，无现成库 |

---

## 目录结构（M6 新增）

```
lmt/src/
├── components/
│   ├── TopologyCanvas.tsx    # UE/BS 拓扑图
│   ├── FsmViewer.tsx         # 状态机视图
│   ├── MscDiagram.tsx        # MSC 时序图
│   └── PduDetail.tsx         # PDU 十六进制查看器
├── hooks/
│   └── usePduStore.ts        # PDU 缓存管理
```

---

## 风险与约束

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| Mermaid.js 动态更新性能 | MSC 消息多时卡顿 | 限制显示最近 50 条消息，支持手动加载更多 |
| WebSocket 消息洪水 | 前端内存溢出 | 后端限流 + 前端滑动窗口（保留最近 200 条） |

---

## 技能卡片清单

```
.skills/
├── m6_1_web_topology.md
├── m6_2_web_fsm_viewer.md
├── m6_3_web_msc.md
└── m6_4_web_pdu_detail.md
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M6.1 拓扑图 | ✅ 已完成 | TopologyCanvas.tsx — SVG, UE/BS 节点 + 连接线状态动画 |
| M6.2 状态机 | ✅ 已完成 | FsmViewer.tsx — MAC/RRC/NAS 三行 FSM 高亮 |
| M6.3 MSC | ✅ 已完成 | MscDiagram.tsx — 箭头列表式 MSC，50 条窗口 |
| M6.4 PDU 查看 | ✅ 已完成 | PduDetail.tsx + usePduStore — hex dump 模态框 + PDU 列表 |

---

## 更新日志

- `2026-05-31`: 初始计划书创建
- `2026-06-01`: M6.1–M6.4 全部实现：TopologyCanvas (SVG)、FsmViewer (MAC/RRC/NAS)、MscDiagram (事件→箭头列表)、PduDetail (hex dump 模态框 + usePduStore hook)。App.tsx 重构为左面板(拓扑+FSM)+右面板标签页(日志/MSC/PDU)。TypeScript 编译通过，63 个 C++ 测试全部通过。

# M5 计划书：可观测性增强 & 数据传输

> **状态**: `PLANNED` — 尚未开始实现。本文档随实现进度持续更新。
> **预计工期**: 2 周
> **前置依赖**: M4 初始附着全流程完成

---

## 目标

在 M4 能跑通完整控制面流程的基础上，让系统**真正可观测、可调试**：
1. 自动生成消息序列图（MSC）
2. 每层 PDU 十六进制 dump 与解析
3. 用户面数据首次贯通（ping 式回环测试）

这是从"能跑"到"能看、能调"的关键转变。

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M5.1** | 消息序列图生成器 | `vis_msc.md` | Python 脚本解析双端日志，输出 PlantUML/Mermaid 时序图；RACH 流程图清晰可读 |
| **M5.2** | PDU 十六进制分析器 | `pdu_analyzer.md` | 每层（MAC/RLC/PDCP/RRC/NAS）收发时 dump hex；Python 脚本可逐层解包 |
| **M5.3** | 用户面数据收发 | `user_plane.md` | UE 发 `"Hello"`，BS 回 `"World"`；全栈 ping 通，RTT 打印在控制台和 LMT |

---

## 接口契约

### PDU Trace 日志事件

每层在收发 PDU 时发送结构化日志：

```json
{
  "timestamp": "2026-05-31T10:30:00.123456Z",
  "module": "UE",
  "level": "DEBUG",
  "event": "PDU_TRACE",
  "fields": {
    "direction": "TX",
    "layer": "MAC",
    "length": 42,
    "hex": "01:02:03:...",
    "brief": "RAR: ra-rnti=0x43FA, ta=12"
  }
}
```

### MSC 生成器输入

Python 脚本 `tools/scripts/generate_msc.py`：
- 输入：UE 和 BS 的日志文件（或实时订阅 WebSocket）
- 输出：`msc.puml` 或 `msc.md`（Mermaid 格式）

---

## 目录结构（M5 新增）

```
tools/
├── scripts/
│   ├── generate_msc.py       # 日志 → PlantUML/Mermaid
│   ├── pdu_analyzer.py       # hex dump 解析器
│   └── latency_report.py     # RTT 统计
```

---

## 风险与约束

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| MSC 图实时生成性能 | 日志量大时卡顿 | 先离线生成，实时版用增量更新 |
| PDU 解析器维护 | 层格式变化后解析器失效 | 解析逻辑用 Python（易修改），与 C++ 层解耦 |

---

## 技能卡片清单

```
.skills/
├── m5_1_vis_msc.md
├── m5_2_pdu_analyzer.md
└── m5_3_user_plane.md
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M5.1 MSC 生成 | 🔴 未开始 | 前置：M4 完成 |
| M5.2 PDU 分析 | 🔴 未开始 | |
| M5.3 用户面 | 🔴 未开始 | |

---

## 更新日志

- `2026-05-31`: 初始计划书创建

# 技术栈与架构

## 技术栈总览

| 层次 | 技术 | 版本 | 用途 |
|------|------|------|------|
| 协议栈 | C++17 | GCC/Clang | PHY/MAC/RLC/PDCP/RRC/NAS/App 全部层实现 |
| 构建系统 | CMake | ≥3.14 | 多目标构建，Google Test 集成 |
| 测试框架 | Google Test | 1.12 | 单元 + 内存空口 E2E，93 个用例（常规+ASan 双构建全绿） |
| Sanitizer | ASan + UBSan | GCC/Clang 内置 | `AETHER_SANITIZE=ON` 内存审计（M7.2） |
| CI | GitHub Actions | - | 构建+ctest+事件目录一致性+LMT tsc/build |
| 参考模型 | Python + NumPy | 3.12 | PHY 黄金参考，QPSK/OFDM/AWGN |
| 日志桥接 | Python + asyncio + websockets | - | UDP→WebSocket，供 LMT 实时消费 |
| 信道仿真 | Python + socket | - | UDP 中继，可配丢包率/延迟 |
| 前端框架 | React | 18.2 | 组件化仪表盘 |
| 前端语言 | TypeScript | 5.2 | 类型安全 |
| 前端构建 | Vite | 5.0 | 快速 HMR 开发 + 生产构建 |
| 分析脚本 | Python | 3.12 | MSC 生成、PDU 解析、RTT 统计、事件目录校验 |
| 验证工具 | Python | 3.12 | 跨进程冒烟、故障恢复场景、30 分钟长跑框架 |

## 协议栈数据流

### 控制面 (Control Plane)

```
NAS (Attach Request/Accept)
  ↕
RRC (Setup Request/Setup/SetupComplete)
  ↕
PDCP (透传 + 2 字节简化头)
  ↕
RLC TM (SDU = PDU，无分段)
  ↕
MAC (PDU 编解码, LCID=0 for CCCH)
  ↕
PHY (QPSK → OFDM → IQ samples → UDP)
```

### 用户面 (User Plane)

```
App (send_data / on_data_received)
  ↕
PDCP (tx/rx + 2 字节头)
  ↕
RLC TM (tm_tx/tm_rx)
  ↕
MAC (build_pdu/parse_pdu, LCID=1..32)
  ↕
PHY (phy_tx/phy_rx + AWGN channel)
```

## 进程间通信

| 通道 | 协议 | 端口 | 数据格式 |
|------|------|------|---------|
| UE ↔ BS (PHY) | UDP | 10001 ↔ 20002 | `iq_to_bytes`: [uint32 count, float re, float im, ...] |
| UE/BS → Log Server | UDP | 9999 | JSON: `{timestamp, module, level, event, fields}` |
| Log Server → LMT | WebSocket | 8765 | 同上 JSON |
| Channel Sim (UE→BS) | UDP | 10001 → 20002 | 透传 IQ 字节流 |
| Channel Sim (BS→UE) | UDP | 20002 → 10001 | 透传 IQ 字节流 |

## 日志格式

所有 C++ 模块通过 `logging::log()` 输出单行 JSON：

```json
{
  "timestamp": "2026-06-01T03:23:02.817381Z",
  "module": "UE",
  "level": "INFO",
  "event": "MAC_RACH_MSG1",
  "fields": {"preamble": "42", "tx_count": "1"}
}
```

日志通过 UDP 发送到 Log Server (port 9999)，再广播到 Web LMT (WebSocket port 8765)。

## 控制面与可观测性平面

```
命令面:   操作者/LMT ──► UE :10101 / BS :10102 (attach/detach/traffic/stats)
观测面:   节点 ──JSON/UDP:9999──► log_server ──WS:8765──► LMT / 工具
事件契约: stack/common/include/common/events.h (75 项) ⇄ lmt/src/events.ts (CI 校验)
```

节点编排层 (`stack/core`, 见 [编排层](../stack/orchestration.md)) 是两侧的枢纽：
对上暴露命令与统计，对下以显式时钟驱动全部层实体。

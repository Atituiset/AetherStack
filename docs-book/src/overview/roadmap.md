# 里程碑路线图

## 概览

| 里程碑 | 内容 | 状态 | 标签 | 测试数 |
|--------|------|------|------|--------|
| M0 | 项目骨架 + Logger + UDP + Web LMT 骨架 | ✅ | `M0` | - |
| M1 | PHY 层: QPSK + OFDM + L1 E2E | ✅ | `M1` | 15 |
| M2 | MAC 层: PDU + RACH 4 步握手 | ✅ | `M2` | 12 |
| M3 | RLC TM + PDCP 透传 + 垂直贯通 | ✅ | `M3` | 4+5+4 |
| M4 | RRC 连接 + NAS 附着 + 全流程 | ✅ | `M4` | 10+4+2 |
| M5 | 可观测性 + 用户面数据 | ✅ | `M5` | 4+3 |
| M6 | Web LMT 核心功能 (4 组件) | ✅ | `M6` | - |
| M7 | 稳定化: 数据回环 + 内存审计 + 异常恢复 | 📋 | - | - |
| M8 | 演示: 一键启动 + 场景剧本 + 交付 | 📋 | - | - |

## 各阶段详细目标

### M0 — 项目骨架
- C++17 CMake 项目结构 (10 个子目录)
- 统一 JSON Logger (`logging::log`, 线程安全, UDP 远程)
- UDP Transport 类 (`UdpTransport`, bind/send/recv)
- Web LMT 骨架 (Vite + React + TypeScript + WebSocket)
- Google Test 集成

### M1 — 物理层闭环
- QPSK 调制解调 (Gray 编码, 归一化 1/√2)
- OFDM 收发 (64-FFT, CP=16, Cooley-Tukey 原地 FFT)
- PHY I/O 序列化 (IQ↔bytes, bits↔bytes)
- L1 E2E: UE→仿真信道→BS, 误码率=0 (高 SNR)
- Python 参考模型交叉验证

### M2 — MAC 层与随机接入
- MAC PDU: 简化子头 [R|F|LCID] + 变长 L, F-bit 扩展
- RACH 4 步: MSG1(PRACH) → MSG2(RAR) → MSG3(RRC Req) → MSG4(CR)
- UE 侧状态机: IDLE → WAIT_RAR → WAIT_CR → CONNECTED
- BS 侧: RA-RNTI 分配, C-RNTI 分配, 竞争解决
- 超时重试: max_preamble_transmissions=3

### M3 — RLC/PDCP 透传
- RLC TM: `tm_tx(sdu)=sdu`, `tm_rx(pdu)=pdu` (零开销透传)
- PDCP: 2 字节简化头 [version|reserved][seq_num], 无 ROHC/加密
- 全链路垂直测试: PDCP→RLC→MAC→PHY→AWGN→回传

### M4 — RRC 与 NAS
- RRC 消息: TLV 格式 [msg_type(1)][length(2)][value(N)]
- MIB: SFN + 带宽; SIB1: PLMN + TAC + Cell ID
- RRC 连接: SetupRequest → Setup → SetupComplete (3 消息握手)
- NAS 附着: AttachRequest(IMSI) → AttachAccept(TMSI)
- 全流程集成: 冷启动 → SIB → RACH → RRC → NAS → REGISTERED

### M5 — 可观测性 & 数据传输
- `generate_msc.py`: JSON 日志 → Mermaid sequenceDiagram
- `pdu_analyzer.py`: Hex PDU → MAC→PDCP→RRC/NAS 逐层解包
- `latency_report.py`: APP_DATA_TX/RX → RTT 统计
- AppLayer: `send_data()`/`on_data_received()`, PDU trace 日志
- 用户面 ping-pong: "Hello" → PHY → "World" 回传

### M6 — Web LMT 核心功能
- TopologyCanvas: SVG 拓扑 (UE 圆 + BS 方块 + 链路动画)
- FsmViewer: MAC/RRC/NAS 三行 FSM 高亮
- MscDiagram: 箭头列表式 MSC (9 种事件, 50 条窗口)
- PduDetail: Hex dump 模态框 + `usePduStore` hook
- App.tsx 重构: 左面板(拓扑+FSM) + 右标签页(日志/MSC/PDU)

### M9 — FEC + HARQ
- 卷积码 K=7 rate 1/2 (133o/171o) + Viterbi 硬判决 + CRC16 传输块校验
- 停等式 HARQ 4 进程: Chase 合并软缓冲、ACK 超时重传、预算耗尽放弃
- 统一帧封装 (magic 0xA9): 用户流量走 HARQ, ACK 控制走传统路径
- 实测: BER 5% 残余 ≈0.1%; 20% 丢帧信道下 101 次 burst 丢失压缩至 ≤2

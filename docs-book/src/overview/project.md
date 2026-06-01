# 项目概览

AetherStack 是一个**从零构建的 5G NR 无线协议栈 MVP**，覆盖从物理层 (L1) 到非接入层 (NAS) 的完整七层协议栈。项目目标是让 UE 和 BS 在一台电脑上通过仿真信道完成完整的通信流程。

## 设计理念

1. **分层技能驱动**：每个子模块以"技能卡片"定义接口契约和验收标准，AI agent 逐步实现
2. **内建可观测性**：JSON 结构化日志贯穿全栈，Python 脚本自动生成 MSC 和 PDU 解析
3. **里程碑可回溯**：Git 标签标记每个里程碑，`git checkout M3` 即可回到对应阶段
4. **教育与演示兼顾**：裁剪 3GPP 规范至最简可运行子集，不追求完整规范合规

## 项目范围（MVP）

| 层 | 实现范围 | 未实现 |
|----|---------|--------|
| PHY | QPSK 调制/解调 + OFDM (64-FFT, CP=16) + AWGN | MIMO, 16QAM/64QAM, 信道估计 |
| MAC | PDU 编解码 + 4 步竞争 RACH | 调度器, HARQ, BSR, PHR |
| RLC | 透明模式 (TM) | UM, AM, 分段/重组, ARQ |
| PDCP | 2 字节简化头透传 | ROHC, 加密, 完整性保护 |
| RRC | MIB/SIB1 广播 + 连接建立/释放 | 测量报告, 切换, 重配 |
| NAS | 简化附着 (IMSI 交换 + TMSI 分配) | 鉴权 AKA, PDU 会话, 安全模式 |
| App | 简单 TX/RX + PDU trace 日志 | IP 路由, QoS |

## 目录结构

```
AetherStack/
├── CMakeLists.txt          # 顶层 CMake (C++17)
├── Makefile                # 便捷构建入口
├── start_demo.sh           # 一键演示启动脚本
├── stack/                  # C++ 协议栈
│   ├── common/             # Logger + UDP Transport
│   ├── phy/                # 物理层: QPSK, OFDM, PHY I/O
│   ├── mac/                # MAC 层: PDU 编解码, RACH UE/BS
│   ├── rlc/                # RLC 层: 透明模式
│   ├── pdcp/               # PDCP 层: 透传实体
│   ├── rrc/                # RRC 层: 消息, UE/BS 实体
│   ├── nas/                # NAS 层: 消息, UE/BS 实体
│   ├── app/                # 应用层: 数据收发
│   ├── ue/                 # UE 可执行文件
│   ├── bs/                 # BS 可执行文件
│   └── tests/              # 全部单元/集成测试
├── tools/                  # Python 工具集
│   ├── log_server/         # UDP → WebSocket 日志桥接
│   ├── channel/            # 仿真信道 (UDP 中继)
│   ├── ref_models/         # PHY 参考模型 (NumPy)
│   └── scripts/            # MSC/PDU/RTT 分析脚本
├── lmt/                    # Web LMT (React + Vite)
│   └── src/
│       ├── App.tsx
│       ├── components/     # TopologyCanvas, FsmViewer, MscDiagram, PduDetail, LogStream
│       └── hooks/          # useWebSocket, usePduStore
├── docs/                   # 里程碑计划书 (m0–m8)
├── .skills/                # 技能卡片 (25+ 张)
└── docs-book/              # 本文档 (mdBook)
```

## 通信架构

```
 UE 进程                              BS 进程
 ┌─────────┐                         ┌─────────┐
 │  App    │                         │  App    │
 │  PDCP   │                         │  PDCP   │
 │  RLC    │                         │  RLC    │
 │  MAC    │ ←── UDP (IQ bytes) ──→  │  MAC    │
 │  PHY    │         ↑               │  PHY    │
 └────┬────┘         │               └────┬────┘
      │              │                    │
      └── UDP ──→ Channel Sim ──→ UDP ───┘
                   (loss/latency)
      │
      └── UDP ──→ Log Server ──→ WebSocket ──→ Web LMT
                   (port 9999)     (port 8765)     (:3000)
```

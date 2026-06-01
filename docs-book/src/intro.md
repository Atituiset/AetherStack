# AetherStack 技术文档

> 从零构建的 5G NR 无线协议栈 MVP —— 可运行、可观测、可调试

AetherStack 是一个教育/演示级 5G 无线协议栈实现，覆盖从物理层 (L1) 到非接入层 (NAS) 的完整协议栈。在一台电脑上运行 UE 和 BS 两个进程，通过仿真信道完成"开机 → 小区搜索 → 随机接入 → RRC 连接 → NAS 附着 → 数据传输"的全流程。

## 核心特性

- **完整协议栈**：PHY / MAC / RLC / PDCP / RRC / NAS / App 全七层
- **双端可运行**：UE 和 BS 独立进程，UDP 仿真信道互连
- **全链路可观测**：JSON 结构化日志 + Python MSC/PDU 分析脚本
- **Web 监控终端**：React + TypeScript 实时 LMT（拓扑/FSM/MSC/PDU）
- **63 个单元测试**：每层独立可测 + 全链路集成测试
- **Git 里程碑标签**：M0–M6 每阶段可 `git checkout` 回溯

## 技术栈

| 组件 | 语言/框架 | 说明 |
|------|----------|------|
| 协议栈主体 | C++17 | CMake + Google Test |
| PHY 参考模型 | Python/NumPy | QPSK + OFDM + AWGN 黄金参考 |
| 日志服务器 | Python/asyncio | UDP → WebSocket 桥接 |
| 信道模拟器 | Python | 可配置丢包率/延迟的 UDP 中继 |
| Web LMT | React 18 + TypeScript + Vite | 暗色主题仪表盘 |
| 分析脚本 | Python | MSC 生成、PDU 解析、RTT 统计 |

## 快速开始

```bash
git clone https://github.com/Atituiset/AetherStack.git
cd AetherStack
make          # 编译 C++ 协议栈
./start_demo.sh  # 一键启动全部组件
```

浏览器打开 http://localhost:3000 查看 Web LMT。

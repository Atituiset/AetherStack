# Python 工具集

AetherStack 提供一组 Python 工具，用于日志聚合、信道模拟、PHY 参考模型和日志分析。

## 工具概览

| 工具 | 位置 | 功能 |
|------|------|------|
| Log Server | `tools/log_server/log_server.py` | UDP→WebSocket 日志桥接 |
| Sim Channel | `tools/channel/sim_channel.py` | UDP 中继 (可配置丢包/延迟) |
| PHY 参考模型 | `tools/ref_models/phy_ref.py` | NumPy QPSK/OFDM/AWGN 参考实现 |
| MSC 生成器 | `tools/scripts/generate_msc.py` | JSON 日志→Mermaid 序列图 |
| PDU 分析器 | `tools/scripts/pdu_analyzer.py` | 十六进制 PDU→逐层解码 |
| 延迟报告 | `tools/scripts/latency_report.py` | RTT 统计分析 |

## 目录结构

```
tools/
├── log_server/
│   └── log_server.py
├── channel/
│   └── sim_channel.py
├── ref_models/
│   └── phy_ref.py
└── scripts/
    ├── generate_msc.py
    ├── pdu_analyzer.py
    └── latency_report.py
```

## 依赖

- Python 3.8+
- `numpy` (仅 phy_ref.py)
- `websockets` (仅 log_server.py)
- 其余脚本仅使用标准库

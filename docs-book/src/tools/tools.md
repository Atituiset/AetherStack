# Python 工具集

AetherStack 的 Python 工具覆盖日志聚合、信道模拟、验证测试与日志分析。

## 工具概览

| 工具 | 位置 | 功能 |
|------|------|------|
| Log Server | `tools/log_server/log_server.py` | UDP→WS 桥接（背压/`_seq`/命令通道） |
| Sim Channel | `tools/channel/sim_channel.py` | 双向中继：丢包/延迟/黑洞/时变曲线 |
| Demo Scenario | `tools/demo/demo_scenario.py` | 无人值守演示编排（M8.2） |
| E2E Smoke | `tools/test_scripts/e2e_smoke.py` | 跨进程生命周期冒烟 |
| Recovery Test | `tools/test_scripts/recovery_test.py` | 五阶段故障恢复场景 |
| Stability Run | `tools/test_scripts/stability_run.py` | 长跑稳定性框架（RSS/停滞守卫） |
| Events Sync Check | `tools/scripts/check_events_sync.py` | 事件目录 C++↔TS 一致性 (CI) |
| PHY 参考模型 | `tools/ref_models/phy_ref.py` | NumPy QPSK/OFDM/AWGN 参考 |
| MSC 生成器 | `tools/scripts/generate_msc.py` | JSON 日志→Mermaid 序列图 |
| PDU 分析器 | `tools/scripts/pdu_analyzer.py` | 十六进制 PDU→逐层解码 |
| 延迟报告 | `tools/scripts/latency_report.py` | RTT 统计分析 |

详见 [验证工具集](./e2e_harness.md) 与 [演示系统](../demo/demo.md)。

## 依赖

- Python 3.10+，虚拟环境 `.venv/`（start_demo.sh 自动创建）
- `websockets`（log_server / demo_scenario）
- `numpy`（仅 phy_ref.py）
- 其余脚本仅使用标准库

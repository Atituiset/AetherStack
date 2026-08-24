# M7.4 异常恢复

## 目标
信道恶化（高丢包/断网）时系统不崩溃；恢复后无需人工干预继续工作。

## 分层自愈设计

| 层 | 机制 | 触发 |
|----|------|------|
| MAC RACH | RAR/CR 窗口超时 → backoff 抖动重发 preamble（上限 3 次）→ IDLE | 单轮接入失败 |
| 编排层 | `ATTACH_RETRY`：RACH 塌陷且 attach 请求仍有效 → 50–200ms 抖动后重置 RRC 并重跑整条附着链 | 守卫窗口(3s)内循环 |
| 守卫层 | attach guard 超时 → `ATTACH_ABORT`：全部状态机静默归零，放弃本次请求 | 3s 未注册 |
| 应用层 | e2e_smoke/recovery_test 对 ping/detach 做应用级重发（TM 无重传，单帧丢失靠上层兜底） | 驱动脚本 |

## 工具

`tools/test_scripts/recovery_test.py` — 五阶段场景：

```
 0-30s  干净      附着 + 流量建立
30-60s  丢包 50%   有损运行，状态保持
60-90s  干净      回环自动恢复
90-105s 黑洞100%  全断，无崩溃
105-135s 干净     流量恢复 + 干净分离
```

```bash
python3 tools/test_scripts/recovery_test.py   # exit 0 = PASS
```

sim_channel 支撑参数：
- `--loss-schedule "30:60:0.5,90:105:1.0"` 时间窗丢包曲线
- `--blackout "600:10,1200:8"` 全断窗口

## 结果 (2026-08-24)
PASS：50% 丢包期 tx=300/rx=247 系统存活；黑洞后 rx 603→677 自动续流；
全程零 ATTACH_ABORT、零崩溃；含一次 RACH 塌陷 ATTACH_RETRY 自愈成功。

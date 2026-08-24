# M7.3 时序稳定性

## 目标
连续运行 30 分钟：无死锁、无崩溃、流量持续、RSS 有界。

## 工具

`tools/test_scripts/stability_run.py`：
- 独立端口段 `--port-base`（默认 15100），可与其它测试并行
- 驱动：attach 重试直至注册 → `traffic on` → 周期采样
- 每进程日志落盘 + 事件计数（TRAFFIC_STATS/APP_RTT/ATTACH_ABORT…）
- 判定：进程存活 / 注册成功 / tx 持续推进（15s 停滞判失败）/ RSS 相对
  60s 预热基线增长不超 `--max-rss-growth`（默认 100%）
- 输出 JSON 报告 `<logdir>/report.json`

```bash
# 完整 DoD 运行（30 分钟，含信道与黑洞注入）
python3 tools/test_scripts/stability_run.py --duration 1800 \
    --channel --loss-rate 0.05 --blackout "600:10,1200:8" \
    --logdir /tmp/aether_stab_30m
```

## 关键实现事实
- 定时器全部走 TimerList（显式时钟驱动，无隐藏线程）
- uint32 ms 时钟 49.7 天回绕——30 分钟量级安全
- logger 每行 flush（stdout 即遥测流），下游采集无批次延迟

## 结果 (2026-08-24)
3 分钟快速档 PASS（tx=1745 rx=1745 loss=0 rtt_avg=0ms）；
30 分钟信道+黑洞长跑结果见 docs/m7_plan.md 更新日志。

# 验证工具集

位置: `tools/test_scripts/`

M7/M8 沉淀的三层验证工具，全部通过 UE UDP 命令口驱动、解析 JSON 日志流判定结果。

## e2e_smoke.py — 跨进程冒烟

```bash
python3 tools/test_scripts/e2e_smoke.py --timeout 12            # 直连
python3 tools/test_scripts/e2e_smoke.py --channel --loss-rate 0.15 --timeout 20
```

验证完整生命周期事件链（SIB→RACH→RRC→NAS→回环→detach），任一缺失即非零退出。
`SMOKE_DUMP=<file>` 可转储采集到的事件流用于诊断。

## recovery_test.py — 故障恢复五阶段场景

```bash
python3 tools/test_scripts/recovery_test.py    # exit 0 = PASS
```

| 时间窗 | 信道条件 | 期望 |
|--------|---------|------|
| 0–30s | 干净 | 附着 + 流量建立 |
| 30–60s | 丢包 50% | 有损运行，状态保持 |
| 60–90s | 干净 | 回环自动恢复 |
| 90–105s | 黑洞 100% | 全断无崩溃 |
| 105–135s | 干净 | 续流 + 干净分离 |

## stability_run.py — 长跑稳定性框架

```bash
python3 tools/test_scripts/stability_run.py --duration 1800 \
    --channel --loss-rate 0.05 --blackout "600:10,1200:8" \
    --port-base 15100 --logdir /tmp/aether_stab_30m
```

* 判定：进程存活 / 注册成功 / tx 持续推进（15s 停滞判失败）/ RSS 相对
  60s 预热基线增长有界（默认 ≤100%）
* 每 5s 采样 tx/rx/loss/RSS，输出 JSON 报告 `<logdir>/report.json`
* `--port-base` 独立端口段，可与其它测试并行运行

## check_events_sync.py — 事件目录一致性 (CI)

```bash
python3 tools/scripts/check_events_sync.py
```

校验 `stack/common/include/common/events.h`（单一事实来源）与
`lmt/src/events.ts` 镜像一致、所有 `ev::<NAME>` 引用已声明。接入
`.github/workflows/ci.yml`。

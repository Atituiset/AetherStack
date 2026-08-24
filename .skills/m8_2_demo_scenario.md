# M8.2 无人值守演示剧本

## 目标
脚本自动完成 开机 → 附着 → 数据传输 → 释放，无需人工干预，并在 LMT 上可视化进度。

## 架构（零新增控制通道）

```
[demo_scenario] --WS 8765 订阅--> log_server   （感知进度）
[demo_scenario] --UDP 10101----> UE           （驱动命令）
[demo_scenario] --UDP 9999 注入 DEMO_PHASE--> log_server --> LMT 横幅
```

阶段事件复用日志管道：module=DEMO / event=DEMO_PHASE /
fields={phase,title,progress,detail}。log_server 原样广播，LMT 按 fields 渲染。

## 阶段机

| phase | progress | 推进条件 |
|-------|----------|---------|
| boot | 5% | 固定延时 |
| attach | 30% | UE:NAS_ATTACH_ACCEPT_RX |
| traffic | 75% | 固定时长 + APP_RTT 计数汇报 |
| release | 92% | BS:NAS_DETACH_RX |
| done | 100% | — |

每步带超时保护（--timeout），失败打印 TIMEOUT 并以非零码退出；
`--loop` 支持循环演示。

## DoD
直连与 start_demo --with-demo 两场景实测 PASS；DEMO_PHASE 经 WS 可见。

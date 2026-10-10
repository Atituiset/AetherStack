# 演示系统

M8 将系统打包为一键可演示的完整产品。

## 一键启动

```bash
./start_demo.sh                       # 交互模式：stdin 命令驱动 UE
./start_demo.sh --with-demo           # 无人值守剧本 + LMT 演示横幅
./start_demo.sh --with-demo --loss-rate 0.05
```

启动顺序：构建检查 → venv/依赖 → log server → channel(D3) → LMT → BS → UE，
每一步 UDP 端口探测确认就绪；`Ctrl+C`（或超时信号）触发防重入 cleanup
并 TERM→KILL 两级回收全部子进程。

## 无人值守剧本

位置: `tools/demo/demo_scenario.py`

```bash
.venv/bin/python3 tools/demo/demo_scenario.py --traffic-secs 20 [--loop]
```

编排方式——不引入任何新控制通道：

```
[demo_scenario] --WS:8765 订阅------> log_server   （感知进度）
[demo_scenario] --UDP:10101--------> UE            （attach/traffic/detach）
[demo_scenario] --UDP:9999 注入----> log_server    （DEMO_PHASE 阶段播报）
```

阶段机：

| phase | progress | 推进条件 |
|-------|----------|---------|
| boot | 5% | 固定延时 |
| attach | 30% | `UE:NAS_ATTACH_ACCEPT_RX` |
| traffic | 75% | 固定时长，统计 APP_RTT 个数 |
| release | 92% | `BS:NAS_DETACH_RX` |
| done | 100% | — |

## LMT 演示横幅

`lmt/src/components/DemoBanner.tsx`：从消息流读取最新
`module=DEMO / event=DEMO_PHASE`（fields: phase/title/progress/detail），
渲染渐变进度条与阶段说明；done 后停留 8s 自动隐藏，无演示事件时零渲染。

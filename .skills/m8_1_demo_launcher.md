# M8.1 一键启动器

## 目标
`./start_demo.sh` 一条命令拉起全部组件，Ctrl+C 优雅回收。

## 行为
- 参数：`--with-demo`（追加无人值守剧本）、`--loss-rate <p>`（信道损伤）
- 顺序：构建检查 → venv/依赖 → log server → channel(D3 拓扑) → LMT → BS → UE
- 健康检查：UDP 端口探测（bind 失败 = 组件就绪），超时报错
- cleanup 带 re-entrancy guard：SIGINT/SIGTERM/EXIT 三者只触发一次回收，
  TERM→KILL 两级兜底

## 关键点
- UE 的 stdin 在后台运行时是 EOF——交互命令走 UDP 10101（M6.5 T9 命令通道）
- 演示模式不依赖 stdin：由 demo_scenario.py 驱动

## DoD
`./start_demo.sh --with-demo --loss-rate 0.05` 实测：全部组件就绪探测通过、
剧本 PASS、Ctrl+C/超时后进程组零残留。

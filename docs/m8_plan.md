# M8 计划书：一键启动与演示脚本

> **状态**: `DONE` — 2026-08-24 完成
> **前置依赖**: M7 系统稳定化完成

---

## 目标

将系统打包为一个**可演示的完整产品**：
1. 一键启动所有组件（C++ 节点、Python 服务、Web LMT）
2. 无人值守的完整流程演示脚本
3. 前端全程可视化

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M8.1** | 启动脚本完善 | `demo_launcher.md` | `./start_demo.sh` 一键启动全部，Ctrl+C 优雅退出 |
| **M8.2** | 无人值守剧本 | `demo_scenario.md` | 脚本自动触发 UE 开机 → 附着 → 数据传输 → 释放，无需人工干预 |
| **M8.3** | 演示模式 LMT | `demo_mode.md` | LMT 增加演示模式 UI，显示当前流程进度条和阶段说明 |
| **M8.4** | 文档与交付 | `delivery.md` | README 完整更新；录制 GIF 演示；项目可克隆即运行 |

---

## 技能卡片清单

```
.skills/
├── m8_1_demo_launcher.md
├── m8_2_demo_scenario.md
├── m8_3_demo_mode.md
└── m8_4_delivery.md
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M8.1 启动脚本 | ✅ 完成 | 健康检查(UDP 端口探测)、--with-demo/--loss-rate 参数、cleanup 防重入、TERM→KILL 兜底 |
| M8.2 无人剧本 | ✅ 完成 | tools/demo/demo_scenario.py：WS 订阅感知进度 + UDP 命令驱动 + DEMO_PHASE 注入，全流程 PASS |
| M8.3 演示模式 | ✅ 完成 | lmt DemoBanner：进度条/阶段说明/自动隐藏；复用日志流零新连接；tsc+build 通过 |
| M8.4 交付 | ✅ 完成 | README 重写（架构/快速开始/测试全集/里程碑表）；技能卡片 m8_1..m8_4 |

---

## 实现要点

- 演示编排不引入任何新控制通道：scenario 以 WS 客户端身份订阅现有
  日志流感知进度，以 UE UDP 命令口驱动，阶段播报以 module=DEMO 的
  DEMO_PHASE 事件注入同一管道，log_server 与协议栈零改动。
- start_demo.sh 的 cleanup 使用 `trap - SIGINT SIGTERM EXIT` 防重入，
  避免 timeout/SIGTERM 场景下重复执行。

## 更新日志

- `2026-05-31`: 初始计划书创建
- `2026-08-24`: M8.1–M8.4 全部完成，里程碑关闭

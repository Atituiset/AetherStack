# M8.4 文档与交付

## 目标
项目达到"克隆即运行"的交付标准。

## 交付物
- `README.md` 重写：架构图（ASCII）、协议栈分层表、Quick Start、
  测试与验证命令全集、里程碑状态表、仓库布局、端口表
- 各里程碑计划书在 docs/，技能卡片在 .skills/，渲染文档在 docs-book/
- CI：.github/workflows/ci.yml（构建+测试+事件目录一致性 + LMT tsc/build）

## 验证命令速查

```bash
make test                                        # 单测
./start_demo.sh --with-demo                      # 一键演示
python3 tools/test_scripts/e2e_smoke.py          # 跨进程冒烟
python3 tools/test_scripts/recovery_test.py      # 故障恢复
python3 tools/test_scripts/stability_run.py --duration 1800   # 长跑
```

## GIF 录制指引（环境无 GUI 录屏时的替代）
1. `./start_demo.sh --with-demo`
2. 浏览器打开 :3000，用系统录屏 / playwright 截录演示横幅推进过程
3. 输出 assets/demo.gif 并嵌入 README

## DoD
README 覆盖全部入口；新会话克隆后 `./start_demo.sh` 可直接运行。

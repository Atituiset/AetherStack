# M5.1 消息序列图生成器

## 目标
Python 脚本解析 UE/BS 双端 JSON 日志，自动生成 Mermaid 时序图，让 RACH/RRC/NAS 流程可视化。

## 范围
- 输入：JSON 日志行（每行一条，来自 stdout 或文件）
- 输出：Mermaid sequenceDiagram 格式
- 消息方向推断：TX → 对方 RX，按 event name 配对
- 支持 RACH (MSG1-4)、RRC (Setup/SetupComplete)、NAS (Attach Request/Accept)

## 接口
```bash
python tools/scripts/generate_msc.py < ue_bs_combined.log > msc.md
```

## 验证标准
1. 输入 RACH 4 步日志，输出完整 Mermaid 时序图
2. 图中 UE/BS 角色正确，消息箭头方向正确
3. 可渲染为 SVG/HTML

## 依赖
- M0.2 统一日志（JSON 格式）

## 产出文件
- `tools/scripts/generate_msc.py`

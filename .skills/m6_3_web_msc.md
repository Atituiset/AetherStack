# M6.3 消息序列图（MSC）

## 目标
实时渲染 Mermaid 时序图，显示 RACH/RRC/NAS 消息交互。

## 范围
- MscDiagram 组件：集成 Mermaid.js 渲染
- 实时更新：新消息追加到图末尾
- 限制最近 50 条消息，支持滚动

## 验证标准
1. 收到消息事件后图自动更新
2. RACH 4 步 / RRC 3 条 / NAS 2 条清晰可读
3. 超过 50 条时自动截断旧消息

## 产出文件
- `lmt/src/components/MscDiagram.tsx`

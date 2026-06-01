# M6.4 PDU 十六进制查看器

## 目标
点击 MSC 图中任意消息，弹出该消息在各层的 hex dump 及字段解析。

## 范围
- PduDetail 组件：模态框显示 hex dump
- 逐层着色：MAC 头蓝色，PDCP 头绿色，RRC/NAS payload 橙色
- 字段名标注在 hex 旁边

## 验证标准
1. 点击消息弹出 PDU 详情
2. 各层头部和 payload 分色显示
3. 关闭按钮正常工作

## 产出文件
- `lmt/src/components/PduDetail.tsx`
- `lmt/src/hooks/usePduStore.ts`

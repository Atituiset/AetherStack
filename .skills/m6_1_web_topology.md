# M6.1 双设备拓扑图

## 目标
Canvas/SVG 画出 UE 和 BS 设备节点，连接线在 PHY 事件时闪烁，表示链路状态。

## 范围
- TopologyCanvas 组件：两个设备节点 + 连接线
- 连接线颜色：绿色=CONNECTED，灰色=IDLE，红色=ERROR
- PHY RX 事件时连接线闪烁动画

## 接口
```tsx
interface TopologyProps {
  ueState: string;
  bsState: string;
  linkState: 'idle' | 'active' | 'error';
}
```

## 验证标准
1. 渲染 UE 和 BS 两个节点
2. linkState 变化时连接线颜色更新
3. 无外部依赖（纯 CSS + Canvas/SVG）

## 产出文件
- `lmt/src/components/TopologyCanvas.tsx`

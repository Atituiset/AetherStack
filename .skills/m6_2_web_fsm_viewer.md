# M6.2 实时状态机视图

## 目标
可视化 MAC/RRC/NAS 各层状态机的当前状态，收到状态变更事件时高亮切换。

## 范围
- FsmViewer 组件：三层状态机（MAC/RRC/NAS）
- 每层显示所有可能状态，当前状态高亮
- 状态变更动画：CSS transition

## 接口
```tsx
interface FsmState {
  mac: string;
  rrc: string;
  nas: string;
}
```

## 验证标准
1. 显示三层状态机
2. MAC_STATE_CHANGE/RRC_STATE_CHANGE/NAS_STATE_CHANGE 事件更新视图
3. 状态切换有视觉过渡

## 产出文件
- `lmt/src/components/FsmViewer.tsx`

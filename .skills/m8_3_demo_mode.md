# M8.3 LMT 演示模式

## 目标
前端在无人值守演示时显示阶段横幅：标题 + 进度条 + 说明文字。

## 实现
- `lmt/src/components/DemoBanner.tsx`
- 数据源：现有消息流中 `module=DEMO && event=DEMO_PHASE` 的最新一条
  （fields.phase/title/detail/progress），不新增任何连接
- done 阶段停留 8s 后自动隐藏；无 DEMO_PHASE 时完全不渲染
- App.tsx 在 header 与 main 之间挂载

## 事件契约（与 m8_2 共享）
```
DEMO_PHASE { phase: boot|attach|traffic|release|done,
             title, progress: "0..100", detail }
```

## DoD
tsc + vite build 通过；scenario 运行时横幅按阶段推进并在结束后隐藏。

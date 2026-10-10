# 事件目录镜像 (`events.ts`)

位置: `lmt/src/events.ts`

C++ 事件目录 `stack/common/include/common/events.h` 的 TypeScript 镜像
（M6.5 D5）。事件名是跨进程契约：协议栈发射、LMT 消费，两侧必须逐字一致。

## 结构

```ts
export const ev = {
  PROCESS_START: 'PROCESS_START',
  MAC_STATE_CHANGE: 'MAC_STATE_CHANGE', // {layer, old_state, new_state}
  RRC_UE_STATE: 'RRC_UE_STATE',         // {old, new}: IDLE|CONNECTING|CONNECTED
  NAS_STATE_CHANGE: 'NAS_STATE_CHANGE', // {old, new}: DEREGISTERED|REGISTERING|REGISTERED
  PDU_TRACE: 'PDU_TRACE',               // {layer, direction, len, hex, brief}
  DEMO_PHASE: 'DEMO_PHASE',             // 演示编排注入 (module=DEMO)
  // ... 共 75 项，每项注释字段契约
} as const

export type EventName = (typeof ev)[keyof typeof ev]

export const MSC_UPLINK: Partial<Record<EventName, string>>   // MSC 上行箭头
export const MSC_DOWNLINK: Partial<Record<EventName, string>> // MSC 下行箭头
```

## 一致性保障

`tools/scripts/check_events_sync.py`（CI 强制执行）：

* events.ts 缺失/多出常量 → FAIL
* C++ 源码出现未声明的 `ev::<NAME>` 引用 → FAIL
* 声明但从未发射 → warn（如 Python 侧注入的 DEMO_PHASE）

## LMT 状态推导约定 (D6)

前端**只读事件 fields**，不猜事件名：

| FSM | 数据源 |
|-----|--------|
| MAC RACH | `MAC_STATE_CHANGE.fields.new_state`（`WAIT_CONTENTION_RESOLVE` 显示为 `WAIT_CR`） |
| RRC | `RRC_UE_STATE.fields.new` |
| NAS | `NAS_STATE_CHANGE.fields.new` |
| 节点存活 | `PROCESS_START` / `PROCESS_EXIT` / HEARTBEAT |
| 遥测卡片 | `PHY_CONFIG`(n_fft/cp_len)、`RRC_SIB1_RX`(plmn/tac/cell_id)、`UE_STATUS`(c_rnti/app_rx) |

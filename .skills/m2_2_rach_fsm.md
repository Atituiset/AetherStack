# M2.2 RACH 状态机引擎

## 目标
定义 RACH（Random Access Channel）的公共类型、状态枚举、消息类型和回调接口，为 UE 和 BS 侧的 RACH 实现提供共享基础设施。

## 范围
- `RachState` 枚举：IDLE / WAIT_RAR / WAIT_CONTENTION_RESOLVE / CONNECTED
- `RachMsgType` 枚举：MSG1_PRACH / MSG2_RAR / MSG3_RRC_REQ / MSG4_CONTENTION_RESOLVE
- 回调类型：`RachSendCallback`、`RachStateCallback`
- 配置结构体：`RachConfig`（preamble_index、RAR 窗口、最大重传次数）

## 接口契约

```cpp
namespace mac {
enum class RachState : uint8_t {
    IDLE = 0, WAIT_RAR = 1, WAIT_CONTENTION_RESOLVE = 2, CONNECTED = 3
};
enum class RachMsgType : uint8_t {
    MSG1_PRACH = 1, MSG2_RAR = 2, MSG3_RRC_REQ = 3, MSG4_CONTENTION_RESOLVE = 4
};
using RachSendCallback = std::function<void(RachMsgType, const std::vector<uint8_t>&)>;
using RachStateCallback = std::function<void(RachState, RachState)>;

struct RachConfig {
    PreambleIndex preamble_index = 42;
    uint16_t rar_window_ms = 10;
    uint16_t content_resolve_window_ms = 20;
    uint8_t max_preamble_transmissions = 3;
};

const char* rach_state_str(RachState s);
}
```

## 状态转换图
```
IDLE --(start_rach)--> WAIT_RAR --(on_rar)--> WAIT_CR --(on_cr)--> CONNECTED
  ↑                       |                       |
  └──(max_retries)────────┘    (cr_timeout)───────┘
```

## 验证标准
1. `rach_state_str` 正确映射 4 种状态
2. `RachConfig` 默认值合理

## 依赖
- M2.1 MAC PDU

## 产出文件
- `stack/mac/include/mac/rach_common.h`
- `stack/mac/src/rach_common.cpp`

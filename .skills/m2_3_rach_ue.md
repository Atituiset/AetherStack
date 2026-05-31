# M2.3 RACH UE 侧

## 目标
实现 UE 侧 RACH 状态机：从 IDLE 触发 → 发送 MSG1 → 等待 RAR → 发送 MSG3 → 等待竞争解决 → CONNECTED。含超时重试逻辑。

## 范围
- `RachUe` 类：封装 UE 侧完整 4 步 RACH 流程
- 状态转换回调：通知上层（RRC）状态变更
- 发送回调：将 MSG1/MSG3 交给下层（PHY/MAC）发送
- RAR 超时重试：指数退避或固定重传（M2 简化版为固定）

## 接口契约

```cpp
namespace mac {
class RachUe {
public:
    explicit RachUe(const RachConfig& config = {});
    void set_send_callback(RachSendCallback cb);
    void set_state_callback(RachStateCallback cb);
    RachState state() const;

    void start_rach();
    void on_rar_received(RaRnti ra_rnti, uint16_t timing_advance, uint8_t ul_grant);
    void on_contention_resolve(uint16_t crnti);
    void on_rar_timeout();
    void on_contention_resolve_timeout();
};
}
```

## 关键逻辑
1. `start_rach()`：仅在 IDLE 状态有效，发送 MSG1，转入 WAIT_RAR
2. `on_rar_received()`：仅在 WAIT_RAR 有效，发送 MSG3，转入 WAIT_CR
3. `on_contention_resolve()`：仅在 WAIT_CR 有效，转入 CONNECTED
4. `on_rar_timeout()`：递增 tx_count，若未超限则重发 MSG1，超限则回到 IDLE
5. `on_contention_resolve_timeout()`：直接回到 IDLE

## 验证标准
1. 初始状态为 IDLE
2. `start_rach()` 后状态为 WAIT_RAR
3. 完整 4 步握手后状态为 CONNECTED
4. 非 IDLE 状态下 `start_rach()` 被忽略
5. RAR 超时重试 max_preamble_transmissions 次后回到 IDLE

## 依赖
- M2.2 RACH 状态机引擎

## 产出文件
- `stack/mac/include/mac/rach_ue.h`
- `stack/mac/src/rach_ue.cpp`
- `stack/mac/tests/test_mac.cpp`（RachUe 部分）

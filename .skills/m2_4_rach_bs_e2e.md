# M2.4 RACH BS 侧 + 双端联调

## 目标
实现 BS 侧 RACH 处理器：接收 MSG1 → 生成 RAR (MSG2) → 接收 MSG3 → 发送竞争解决 (MSG4)。并与 UE 侧进行内存中的 E2E 联调测试。

## 范围
- `RachBs` 类：处理 MSG1/MSG3，生成 MSG2/MSG4
- UE 上下文管理：`UeContext` 结构体，按 RA-RNTI 索引
- C-RNTI 分配：递增分配
- E2E 测试：UE 和 BS 在同一进程内通过回调交换消息

## 接口契约

```cpp
namespace mac {
class RachBs {
public:
    RachBs() = default;
    void set_send_callback(RachSendCallback cb);
    void set_state_callback(RachStateCallback cb);

    void on_prach_received(PreambleIndex preamble_idx);
    void on_msg3_received(RaRnti ra_rnti, const std::vector<uint8_t>& msg3_data);

    struct UeContext {
        RaRnti ra_rnti = 0;
        uint16_t c_rnti = 0;
        PreambleIndex preamble = 0;
        bool rach_complete = false;
    };

    const UeContext* find_ue(RaRnti ra_rnti) const;
    bool is_rach_complete(RaRnti ra_rnti) const;
};
}
```

## 关键逻辑
1. `on_prach_received()`：生成 RA-RNTI = 0x4300|preamble_idx，分配 TA=12, UL grant=5，发送 MSG2
2. `on_msg3_received()`：查找 RA-RNTI 对应的 UE 上下文，分配 C-RNTI，发送 MSG4，标记 rach_complete=true
3. 未知 RA-RNTI 的 MSG3 被忽略

## E2E 测试流程
```
UE.start_rach() → MSG1 → BS.on_prach_received() → MSG2 →
UE.on_rar_received() → MSG3 → BS.on_msg3_received() → MSG4 →
UE.on_contention_resolve() → CONNECTED
```

## 验证标准
1. BS 收到 MSG1 后正确发送 MSG2
2. BS 收到 MSG3 后正确发送 MSG4 并分配 C-RNTI
3. 未知 RA-RNTI 的 MSG3 被安全忽略
4. E2E：UE 从 IDLE → CONNECTED，BS 标记 rach_complete=true

## 依赖
- M2.3 RACH UE 侧

## 产出文件
- `stack/mac/include/mac/rach_bs.h`
- `stack/mac/src/rach_bs.cpp`
- `stack/mac/tests/test_mac.cpp`（RachBs + RachE2E 部分）

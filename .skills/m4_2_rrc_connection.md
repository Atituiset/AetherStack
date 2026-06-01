# M4.2 RRC 连接建立

## 目标
实现 UE 和 BS 侧的 RRC 连接建立流程：RRC Setup Request → RRC Setup → RRC Setup Complete。双端状态机进入 RRC_CONNECTED。

## 范围
- RRC UE 状态机：IDLE → CONNECTING → CONNECTED
- RRC BS 侧：处理 Setup Request，分配 C-RNTI，发送 Setup
- 消息格式：简化 TLV（MsgType + length + value）
- 通过 PDCP/RLC/MAC 层传递 RRC 消息

## 接口契约

```cpp
namespace rrc {
enum class UeState { IDLE, CONNECTING, CONNECTED };

class RrcUe {
public:
    UeState state() const;
    void set_send_callback(std::function<void(const std::vector<uint8_t>&)> cb);
    void start_connection();
    void on_message(const std::vector<uint8_t>& pdu);
};

class RrcBs {
public:
    void set_send_callback(std::function<void(uint16_t rnti, const std::vector<uint8_t>&)> cb);
    void handle_message(uint16_t rnti, const std::vector<uint8_t>& pdu);
    bool is_ue_connected(uint16_t rnti) const;
};
}
```

## RRC 消息格式（简化 TLV）
```
[1 byte: msg_type] [2 bytes: length] [N bytes: value]
msg_type: 1=SetupRequest, 2=Setup, 3=SetupComplete, 4=Release
```

## 验证标准
1. UE 发起连接后状态为 CONNECTING
2. BS 收到 Setup Request 后分配 C-RNTI 并回复 Setup
3. UE 收到 Setup 后回复 SetupComplete，状态为 CONNECTED
4. E2E：UE+BS 完成完整 RRC 连接建立

## 依赖
- M4.1 RRC 系统消息

## 产出文件
- `stack/rrc/include/rrc/rrc_ue.h`
- `stack/rrc/include/rrc/rrc_bs.h`
- `stack/rrc/include/rrc/rrc_types.h`
- `stack/rrc/src/rrc_ue.cpp`
- `stack/rrc/src/rrc_bs.cpp`
- `stack/rrc/tests/test_rrc_connection.cpp`

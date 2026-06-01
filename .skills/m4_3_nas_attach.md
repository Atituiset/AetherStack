# M4.3 NAS 简化附着

## 目标
实现 NAS 层简化附着流程：UE 发送 Attach Request（含 IMSI），BS 返回 Attach Accept，NAS 状态变为 REGISTERED。

## 范围
- NAS UE 状态机：DEREGISTERED → REGISTERING → REGISTERED
- NAS BS 侧：处理 Attach Request，分配 TMSI，回复 Attach Accept
- NAS 消息格式：简化 TLV
- 不做 AKA 鉴权，只做 IMSI 交换

## 接口契约

```cpp
namespace nas {
enum class UeState { DEREGISTERED, REGISTERING, REGISTERED };

class NasUe {
public:
    UeState state() const;
    void set_send_callback(std::function<void(const std::vector<uint8_t>&)> cb);
    void send_attach_request(const std::string& imsi);
    void on_message(const std::vector<uint8_t>& pdu);
    const std::string& assigned_tmsi() const;
};

class NasBs {
public:
    void set_send_callback(std::function<void(uint16_t tmsi, const std::vector<uint8_t>&)> cb);
    void handle_message(uint16_t tmsi, const std::vector<uint8_t>& pdu);
    bool is_ue_registered(uint16_t tmsi) const;
};
}
```

## NAS 消息格式（简化 TLV）
```
[1 byte: msg_type] [2 bytes: length] [N bytes: value]
msg_type: 1=AttachRequest, 2=AttachAccept, 3=AttachReject, 4=Detach
AttachRequest value: IMSI string (ASCII)
AttachAccept value: [4 bytes TMSI]
```

## 验证标准
1. UE 发送 Attach Request 后状态为 REGISTERING
2. BS 收到后分配 TMSI 并回复 Attach Accept
3. UE 收到 Attach Accept 后状态为 REGISTERED
4. E2E：UE+BS 完成附着流程

## 依赖
- M4.2 RRC 连接建立

## 产出文件
- `stack/nas/include/nas/nas_ue.h`
- `stack/nas/include/nas/nas_bs.h`
- `stack/nas/include/nas/nas_messages.h`
- `stack/nas/src/nas_ue.cpp`
- `stack/nas/src/nas_bs.cpp`
- `stack/nas/src/nas_messages.cpp`
- `stack/nas/tests/test_nas_attach.cpp`

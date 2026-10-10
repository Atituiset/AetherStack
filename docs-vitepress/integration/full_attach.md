# 完整附着流程测试

位置: `stack/tests/test_full_attach.cpp` (2 个测试)

这是 AetherStack 最重要的集成测试，验证从冷启动到 NAS 注册的完整 5G 附着流程。

## FullAttach.ColdStartToRegistered

### 测试流程

```
1. BS 广播 MIB/SIB1
   bs.broadcast_mib()  → ue.on_mib_received()
   bs.broadcast_sib1() → ue.on_sib1_received()

2. RACH 四步握手
   mac_ue.start_rach()
     → MSG1 → bs.on_prach_received()
     → MSG2 → mac_ue.on_rar_received()
     → MSG3 → bs.on_msg3_received()
     → MSG4 → mac_ue.on_contention_resolve(crnti)

3. RRC 连接建立
   rrc_ue.start_connection()
     → RRC SetupRequest → bs.handle_message()
     → RRC Setup → rrc_ue.on_message()
     → RRC SetupComplete → bs.handle_message()

4. NAS 附着
   nas_ue.send_attach_request(imsi)
     → AttachRequest → nas_bs.handle_message()
     → AttachAccept → nas_ue.on_message()

5. 验证
   mac_ue.state()  == CONNECTED
   rrc_ue.state()  == rrc::UeState::CONNECTED
   nas_ue.state()  == nas::UeState::REGISTERED
   nas_ue.assigned_tmsi() != 0
   rrc_ue.assigned_crnti() != 0
```

## FullAttach.RachRetrySuccess

模拟 RAR 超时后重试成功的场景:

1. `start_rach()` → MSG1 发送
2. `on_rar_timeout()` → MSG1 重发 (tx_count=1)
3. `on_rar_received()` → MSG3 发送
4. `on_contention_resolve()` → CONNECTED

验证: 超时重试不重置计数器，最终仍可成功完成 RACH。

## 回调管道

UE 和 BS 之间通过回调函数连接，模拟无线信道:

```cpp
// UE → BS 管道
ue_entity.set_send_callback([&bs](const auto& pdu) {
  bs_entity.handle_message(pdu);
});

// BS → UE 管道
bs_entity.set_send_callback([&ue](auto rnti, const auto& pdu) {
  ue_entity.on_message(pdu);
});
```

# RRC 测试

## 测试套件概览 (10 个测试)

| 文件 | 测试数 | 覆盖范围 |
|------|--------|---------|
| `test_rrc.cpp` | 10 | 消息编解码, UE/BS 实体, 连接流程 |

## 消息编解码测试

| 测试名 | 验证内容 |
|--------|---------|
| `MibEncodeDecode` | MIB 编码→解码恢复原始字段 |
| `Sib1EncodeDecode` | SIB1 编码→解码, plmn_id 正确恢复 (需 `clear()`) |
| `RrcMessageEncodeDecode` | RrcMessage TLV round-trip |
| `GenerateMib` | `generate_mib(sfn)` 返回正确 SFN |
| `GenerateSib1` | `generate_sib1()` 返回默认值 |

## UE 实体测试

| 测试名 | 验证内容 |
|--------|---------|
| `UeStartConnection` | `start_connection()` 发送 SETUP_REQUEST, 状态→CONNECTING |
| `UeReceiveSetup` | 收到 SETUP 后提取 C-RNTI, 发送 SETUP_COMPLETE, 状态→CONNECTED |
| `UeReceiveRelease` | 收到 RELEASE 后状态→IDLE |

## BS 实体测试

| 测试名 | 验证内容 |
|--------|---------|
| `BsHandleSetupRequest` | 收到 SETUP_REQUEST 后分配 C-RNTI, 回复 SETUP |
| `BsHandleSetupComplete` | 收到 SETUP_COMPLETE 后标记 UE 为 CONNECTED |

## SIB1 解码陷阱

`Sib1::decode()` 中 `plmn_id` 必须在赋值前 `clear()`，否则会出现双重前缀:

```cpp
// 错误: plmn_id 未清空 → "4600146001"
// 正确:
Sib1 Sib1::decode(const std::vector<uint8_t>& data) {
  Sib1 sib1;
  sib1.plmn_id.clear();  // ← 关键!
  // ... 解析赋值
  return sib1;
}
```

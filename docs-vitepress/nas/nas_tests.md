# NAS 测试

## 测试套件概览 (4 个测试)

| 文件 | 测试数 | 覆盖范围 |
|------|--------|---------|
| `test_nas.cpp` | 4 | 消息编解码, UE 附着, BS 处理 |

## 测试详解

| 测试名 | 验证内容 |
|--------|---------|
| `NasMessageEncodeDecode` | NasMessage TLV round-trip |
| `UeAttachRequest` | `send_attach_request()` 发送 ATTACH_REQUEST, 状态→REGISTERING |
| `UeAttachAccept` | 收到 ATTACH_ACCEPT 后提取 TMSI, 状态→REGISTERED |
| `BsHandleAttach` | BS 收到 ATTACH_REQUEST 后分配 TMSI, 回复 ATTACH_ACCEPT, `is_ue_registered()`=true |

## 关键验证

- IMSI 字符串在 Attach Request 中正确编码为 ASCII 字节
- TMSI 在 Attach Accept 中正确编码为 4 字节大端序
- BS 的 `next_tmsi_` 从 0x00010001 起递增
- `is_ue_registered(tmsi)` 在附着完成前返回 false, 完成后返回 true

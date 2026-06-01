# MAC 测试

## 测试套件概览 (12 个测试)

| 套件 | 文件 | 测试数 | 覆盖范围 |
|------|------|--------|---------|
| MacPdu | `test_mac_pdu.cpp` | 4 | PDU 编解码 round-trip |
| RachFsm | `test_rach_fsm.cpp` | 7 | UE 状态机, 超时重试, BS 响应 |
| RachE2e | `test_rach_e2e.cpp` | 1 | UE+BS 双端完整 RACH |

## RACH FSM 测试详解

| 测试名 | 验证内容 |
|--------|---------|
| `StartRachSendsMsg1` | `start_rach()` 发送 MSG1, 状态变为 WAIT_RAR |
| `RarReceivedSendsMsg3` | 收到 MSG2 后发送 MSG3, 状态变为 WAIT_CR |
| `ContentionResolveSuccess` | 收到 MSG4 后状态变为 CONNECTED |
| `RarTimeoutRetries` | RAR 超时后重发 MSG1 (tx_count 递增) |
| `MaxRetriesGivesUp` | 3 次重试失败后回退 IDLE, 日志 `RACH_FAILED` |
| `StartRachWhileWaitingIgnored` | WAIT_RAR 状态下再次 `start_rach()` 被忽略 |
| `BsHandlesMsg1AndMsg3` | BS 正确分配 RA-RNTI 和 C-RNTI |

## E2E 测试

`RachE2e.FullFourStepHandshake`: UE 和 BS 通过回调管道完成完整四步握手，最终验证:
- `rach_ue.state() == RachState::CONNECTED`
- `rach_bs.is_rach_complete(ra_rnti) == true`
- C-RNTI 已分配

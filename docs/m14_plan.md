# M14 计划书：移动性 — 测量报告 / 切换 / 寻呼 / 重建立

> **状态**: `DONE`（commit 76e9f1b）
> **前置依赖**: M10（PCI/小区标识）、M11（多小区）、M12（安全上下文迁移）
> **对应真实能力**: 3GPP NR 连接态移动性（测量报告 → HO 命令 → 随机接入到目标小区）、寻呼、RLF 检测与 RRC 重建立

---

## 差距

M13 及之前：UE 附着到一个小区后"钉死"。没有邻区概念，没有切换；gNB 消失 =
会话永久丢失，只能靠用户手动重新 attach。真实系统里 UE 持续测量邻区、网络
决定切换时机、无线链路失败后 UE 自主重建立并尽量保住 NAS 上下文。

## 设计决策（简化但不失真）

### D1 测量报告
* SIB1 扩展 `cell_id / plmn_id / tac`（`BsNodeConfig`），UE 对听到的每个小区
  维护 `{rx_count, last_seen_ms}`（SIB 接收次数作为强度代理）
* 新 RRC 消息 `MEAS_REPORT`：UE 周期上报 `[n:1]{[cell_id:2][strength:1]}`
* gNB 侧策略：**仅当服务小区从报告中消失且另一小区可闻时触发 HO**
 （共视邻区不触发，避免乒乓）

### D2 切换（X2-like）
* `BsNode::HoCoordinator` 回调模拟 X2 接口，由 owner（测试/进程主循环）接线
* 源侧 `request_handover()` 打包 `HoContext{tmsi, imsi, up_key, sec_on}`
* 目标侧 `prepare_handover()` 预留上下文并分配新 C-RNTI（避开 RACH 区间）
* UE 收 `HO_COMMAND` 后直接在目标小区 RACH（MSG3 带 HO_COMPLETE），数据面
  flow / AM 实体 / 安全密钥随 C-RNTI 迁移，NAS 注册保持

### D3 寻呼
* `BsNode::page(imsi)` 把一条 paging 记录搭在下一次 SIB 广播上
 （新 `LCID_PAGING`），空闲 UE 比对 IMSI 后发起 service request（fresh attach）

### D4 RLF + 重建立
* UE 侧看门狗：CONNECTED 态超过 `radio_link_failure_ms` 无任何下行 → `RLF_DETECTED`
* 重建立：RACH MSG3 携带 `REESTABLISHMENT_REQUEST{old_crnti, cell_id}`
* 目标 gNB 有上下文 → 分配新 C-RNTI，回 `REESTABLISHMENT_OK`，
  数据面状态整体搬家（flow、tmsi 映射、AM 实体）；无上下文 → FAILURE，
  UE 回退全量 attach（NAS deregistered，重新走鉴权）

## 可观测性

新增事件（events.h）：`MEAS_REPORT_TX / HO_TRIGGERED / HO_COMMAND_TX /
HO_COMPLETE_RX / PAGE_TX / PAGE_RX / RLF_DETECTED / RRC_REEST_REQ_TX /
RRC_REEST_OK / RRC_REEST_FAIL`

## DoD（全部达成）

1. E2E：双小区间手动+自动切换，注册与安全上下文保持，业务不中断
2. E2E：寻呼空闲 UE → 自动 service request → 注册
3. E2E：静空触发 RLF → 同 gNB 重建立成功，TMSI/NAS 上下文不变
4. E2E：gNB 重启（BLS）→ 重建立失败 → 全量 attach 到新实例恢复业务
5. 全量回归 140 用例通过

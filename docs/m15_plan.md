# M15 计划书：核心网进程分离 — AMF / UPF

> **状态**: `IN_PROGRESS`
> **前置依赖**: M11（多小区）、M12（鉴权/会话密钥）、M14（切换时上下文迁移）
> **对应真实能力**: 5G 核心网解耦 — gNB 通过 NGAP 对接 AMF（控制面）、通过
> GTP-U 对接 UPF（用户面锚点）；gNB 不再持有订阅者数据库

---

## 差距

M14 及之前：`nas::NasBs`（假 MME）内嵌在 `BsNode` 里——注册表、鉴权、TMSI
分配、会话密钥全在 gNB 进程内。后果：

1. gNB 重启 = 核心网一起消失（M14 测试里 bs_b 必须重新走全量 attach）
2. 切换需要逐字段拷贝 `HoContext`，本质是"把核心网状态在 gNB 间搬运"
3. 用户面数据在 gNB 内部直接环回，没有真正的锚点（多 gNB 间无法无缝移动）

## 设计决策（简化但不失真）

### D1 进程边界与接口
* 新进程 `amfd`：订阅者库（USIM 密钥）、鉴权（HMAC-SHA256 AKA）、TMSI 分配、
  注册状态机、会话密钥保管 —— 即现有 `NasBs` 的逻辑整体搬家
* 新进程 `upfd`：用户面锚点。持有每 UE 的下行队列，gNB 上行数据转发到 UPF，
  UPF 下行数据按 UE 当前 serving gNB 路由回去 —— 切换只改 UPF 里的路由表项
* 接口走现有 `UdpTransport` + JSON 事件日志（复用 M6.5 的跨进程模式），
  消息语义对齐 NGAP/GTP-U：
  * NG-like（gNB ↔ AMF）：`NG_SETUP / INITIAL_UE_MSG / UPLINK_NAS /
    DOWNLINK_NAS / UE_CONTEXT_RELEASE / PAGING / HANDOVER_REQUIRED|COMMAND|NOTIFY`
  * GTP-U-like（gNB ↔ UPF）：`UL_DATA{rnti, payload}` / `DL_DATA{rnti, payload}`
    / 路径更新 `PATH_SWITCH{rnti, gnb_id}`
* 二进制编码沿用 NasMessage/RrcMessage 风格；不引入 protobuf（项目哲学：
  简化但不失真）

### D2 BsNode 改造
* `NasBs` 从 BsNode 剥离，NAS PDU 通过 UDP 透传给 AMF（UPLINK/DOWNLINK_NAS）
* 鉴权成功后 AMF 把会话密钥推给 gNB（对齐真实 NGAP 的 key 交付），gNB 只做
  空口加解密，不再保管订阅者长期密钥
* 寻呼由 AMF 发起（PAGING → gNB SIB 广播），与 M14 的 paging 通道对接

### D3 切换语义升级（依赖 M14 HoCoordinator）
* HO 准备从"目标 gNB 拷贝上下文"改为"AMF 仲裁"：源 gNB HANDOVER_REQUIRED →
  AMF 向目标 gNB 下发上下文 + 通知 UPF 做 PATH_SWITCH → 目标 gNB 回确认
* UE 视角协议不变（仍是 M14 的 MEAS_REPORT → HO_COMMAND → RACH+HO_COMPLETE）

### D4 单进程回退模式
* 库形态提供 `core::AmfEntity` / `core::UpfEntity`，可被测试在同一进程内直连
 （in-memory transport）；独立 main 只是薄壳 —— 沿用 ue/bs 的分层哲学

## DoD

1. 单测：AmfEntity 鉴权/注册/密钥交付与原 NasBs 行为等价
2. E2E（同进程）：UE attach 经 gNB→AMF 完成；业务数据经 gNB→UPF→gNB 环回，
   应用层无感知
3. E2E（真 UDP 多进程）：bs + amf + upf 三进程跑通 attach 与 ping-pong
4. 移动性升级：跨 gNB 切换后 UPF 锚点不变，业务流零丢失（对比 M14 的上下文拷贝）
5. gNB 重启后 AMF 注册表仍在 → UE 重建立/重 attach 时核心网侧免鉴权恢复
6. 全量回归通过；LMT 事件目录收录新 NG/GTP 事件

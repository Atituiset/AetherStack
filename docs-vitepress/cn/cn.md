# 核心网层 (M15)

M15 把内嵌在 gNB 里的假 MME 拆分为独立的核心网实体，gNB 回归纯无线节点。
代码位于 `stack/cn/`，独立进程为 `amfd` / `upfd`。

## 进程与接口拓扑

```
UE ══空口══ gNB (bs) ──NG-like──> amfd        HSS / AKA / TMSI / 会话密钥
                  ──GTP-U-like──> upfd        用户面锚点 + 路由表
```

两条接口复用同一套二进制信封（`cn/cn_messages.h`）：

```
[type:1][len:2 LE][value...]
```

### NG-like 消息族（控制面，语义对齐 NGAP）

| 消息 | 方向 | 载荷 |
|------|------|------|
| NG_SETUP / NG_SETUP_OK | gNB→AMF / AMF→gNB | `{cell_id:2}` |
| INITIAL_UE_MSG | gNB→AMF | `{rnti:2} ++ nas_pdu`（首条上行 NAS，UE 未知名） |
| UPLINK_NAS / DOWNLINK_NAS | 双向 | `{tmsi:4}{rnti:2}{len:2} ++ nas_pdu` |
| SESSION_KEY | AMF→gNB | `{tmsi:4}{rnti:2} key(32)` — 鉴权后交付，gNB 由此开启空口加密 |
| PAGING_REQ | AMF→gNB | imsi |
| UE_CTX_RELEASE | gNB→AMF | `{tmsi:4}` |
| HO_REQUIRED / HO_COMMAND | 源 gNB→AMF / AMF→目标 gNB | `{tmsi:4}{tgt:2}++ctx(sec_on,key,imsi)` |
| HO_NOTIFY / HO_PREPARED | 目标→AMF / AMF→全体 gNB | `{tmsi,new_rnti(,tgt)}` |

### GTP-U-like 消息族（用户面，语义对齐 GTP-U）

| 消息 | 方向 | 载荷 |
|------|------|------|
| UL_DATA | gNB→UPF | `{tmsi:4}{rnti:2} ++ payload`（顺带学习路由） |
| DL_DATA | UPF→gNB | `{tmsi:4}{rnti:2} ++ payload`（按锚点路由表转发） |
| PATH_SWITCH / 首包 sighting | gNB→UPF / UPF 日志 | 路由表更新 = 切换的用户面落地 |

## 实体

### Amf (`cn/amf.h`)

* **HSS**：`add_subscriber(imsi, key)` 注册 USIM 主键；未知 IMSI 走开放接入（测试便利，语义与 M12 一致）
* **AKA**：AUTH_REQUEST(RAND) → 校验 RES=HMAC-SHA256(K,RAND)；错 RES 拒绝并告警
* **会话密钥**：`session_k = HMAC(K, RAND‖"up-enc")`，ATTACH_ACCEPT 之后经 SESSION_KEY 推给 gNB
* TMSI 分配自 `0x00010001` 递增

### Upf (`cn/upf.h`)

* 锚点路由表 `routes_[tmsi] = {rnti, cell}`；UL_DATA 首次 sighting 或变化即记日志（UPF_PATH_SWITCH）
* `send_downlink()` 是"互联网侧"注入点；无路由则 UPF_NO_ROUTE 告警
* 测试/演示可挂 `ul_sink`（回声）与 `dl_sink`

## 载体 CnLink (`cn/cn_link.h`, `cn/udp_cn_link.h`)

| 实现 | 用途 |
|------|------|
| `InMemoryCnLink` | 同进程直连，`connect_to` 建立双向 peer（多 peer 扇出：一个 AMF 可服务多个 gNB）；单测与默认接线用 |
| `UdpCnLink` | 真进程：client 模式 `set_remote` 固定远端；server 模式依赖 `UdpTransport::recv_from/reply` 回最近请求方 |

## gNB 侧接线 (`BsNode::attach_core`)

```cpp
core::BsNode::CnEndpoints ep;
ep.amf = &ng_link;   ep.upf = &gu_link;   ep.gnb_cell = 1;
bs.attach_core(ep);
```

接线后的行为变化：

1. NAS PDU 对 gNB 完全透明——UL 解出即 `UPLINK_NAS` 上送；DL_NAS/SESSION_KEY/DL_DATA 反向下发
2. 用户面上行终结于 UPF（不再本地回环）
3. 内嵌 `nas_bs_` 与本地 echo 全部旁路；**不调用 attach_core 则保持 M14 及之前的单节点行为**（旧测试零改动通过）

命令行等价：`bs --amf-addr 127.0.0.1 --amf-port 10110 --upf-addr 127.0.0.1 --upf-port 10120`。

## AMF 仲裁的切换流程

```
源 gNB                AMF                    目标 gNB
  │ HO_REQUIRED ──────>│                        │
  │                    │ HO_COMMAND ──────────> │ prepare_handover()
  │                    │ <──────────── HO_NOTIFY │ 分配新 C-RNTI
  │ <─ HO_PREPARED ────│ (含 tgt + new_rnti)     │
  │ RRC HO_COMMAND → UE（经空口）                │
  │      UE RACH 到目标小区，HO_COMPLETE ───────> │
  │ UE_CTX_RELEASE ──> │                        │
```

X2-like 直连 coordinator 不再是必需路径；两种模式并存（设置了 coordinator 则优先直连）。

## 验证

* 单测/E2E（同进程）：分离核心网 attach、鉴权 attach + 密钥交付加密回环、gNB 重启恢复（AMF/UPF 状态存活）
* 多进程冒烟：`python3 tools/test_scripts/e2e_smoke_m15.py` — amfd+upfd+bs+ue 四真进程全生命周期

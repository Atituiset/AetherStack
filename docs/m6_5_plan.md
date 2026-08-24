# M6.5 计划书：跨进程端到端串联（真正的全栈贯通）

> **状态**: `IN_PROGRESS` — 2026-08-24 开始实现
> **前置依赖**: M0–M6 全部完成
> **背景**: 深度分析发现 `ue`/`bs` 进程只链接了 PHY，MAC/RRC/NAS/RLC/PDCP/App 只在单测中存活；
> 跨进程 attach 只存在于单进程内存回调测试。本里程碑把整栈接入真实进程，并修复阻塞演示的 P0 缺陷。

---

## 目标

1. UE↔BS 两个真实进程之间完成：SIB 接收 → RACH 四步 → RRC 建立 → NAS 附着 → 用户面数据回环 → 释放
2. 流量真正经过信道模拟器（丢包/延迟生效），端口零冲突
3. LMT 前端与协议栈事件目录对齐，实况点亮拓扑/FSM/MSC/PDU 视图
4. 新增可测试的节点编排层（UeNode/BsNode），进程 main 变薄壳

---

## 关键设计决策

### D1 空口帧格式（PHY 解码后的比特域，字节对齐）

| 帧类型 | 字节布局 | 说明 |
|--------|---------|------|
| `0xA1 MSG1` | `[type][preamble]` | PRACH 前导 |
| `0xA2 MSG2` | `[type][rar 原始字节...]` | RAR（rach 层编码） |
| `0xA3 MSG3` | `[type][ra_rnti:2][CCCH RRC PDU...]` | 竞争接入，携带真实 RRC SetupRequest |
| `0xA4 MSG4` | `[type][msg4 原始字节...]` | 竞争解决 |
| `0xA5 DATA` | `[type][rnti:2][MAC PDU...]` | 数据/专用控制复用帧 |
| `0xFF` | — | 无效类型，接收端丢弃并告警 |

比特数补齐到偶数（QPSK 要求）。UDP 载荷仍为 IQ 序列化格式不变。

### D2 MAC LCID 分配

| LCID | 用途 |
|------|------|
| 0 | CCCH（RRC 建立，经 RLC/PDCP 直通） |
| 1 | NAS DCCH（经 PDCP+RLC） |
| 2 | APP DTCH（经 PDCP+RLC） |
| 61 / 62 | SIB1 / MIB 系统消息（BS 广播，rnti=0xFFFF） |
| 63 | PADDING（已有） |

### D3 端口拓扑（消除 bind 冲突）

| 角色 | 端口 |
|------|------|
| UE PHY 监听 | 10001（不变） |
| BS PHY 监听 | 20002（不变） |
| 信道模拟上行入口 | 11001 → 转发 BS:20002 |
| 信道模拟下行入口 | 21002 → 转发 UE:10001 |
| UE 命令口（M8.2） | 10101 |
| 日志 UDP / WS | 9999 / 8765（不变） |

直连模式为二进制默认值；演示脚本通过 `--bs-phy-port 11001` / `--ue-phy-port 21002` 把流量引入信道。

### D4 节点编排层（新库 stack/core）

- `UeNode` / `BsNode`：持有全部层实体，提供 `on_air_rx(bytes)`、`tick(now_ms)`、命令方法；
  空口对端在测试中用内存管道对接，进程 main 只做 socket poll + timer tick。
- 定时器表：RAR 窗口 250ms、CR 窗口 500ms、SIB 周期 200ms、心跳 5s、附着守卫 3s、RACH 退避抖动 100–300ms。
- MSG3 由 `RachUe` 的 msg3 provider 回调取真实 RRC SETUP_REQUEST 字节。
- C-RNTI 单一来源：RachBs 分配后传给 RrcBs/RrcUe 使用，废除双计数器。

### D5 事件目录（单一事实来源）

- `stack/common/include/common/events.h`：全部事件名常量 + 注释契约
- `lmt/src/events.ts`：镜像（CI grep 校验一致性）
- 补齐缺失发射：`NAS_ATTACH_REQUEST_TX`、`NAS_STATE_CHANGE`、`NAS_ATTACH_ACCEPT_RX`、`PROCESS_EXIT`、`HEARTBEAT`、`APP_RTT`
- 新增 `PDU_TRACE` 发射点：UE/BS 各层的 TX/RX hop（hex 截断 48B）

### D6 前端修复

- 状态推导改用事件 fields（`MAC_STATE_CHANGE.new_state` 等），不再猜事件名
- PDU store 改为按 `_seq` 增量处理，消除 O(n²) 重复
- 遥测卡片从 `PHY_CONFIG`/`RRC_SIB1_RX` 取真值

---

## 任务分解

| # | 任务 | DoD | 状态 |
|---|------|-----|------|
| T1 | core/radio_frames 编解码 | 单测往返 + 非法输入拒收 | ✅ 完成 (M6.5 T1-T5) |
| T2 | core/timer_list | 单测：到期/取消/周期 | ✅ 完成（后续修复绝对时钟锚点） |
| T3 | phy_io 越界修复 + phy_rx 自动长度 | 单测含畸形输入 | ✅ 完成 (M6.5 T1-T5) |
| T4 | RachUe MSG3 provider + RachBs 上抛 | mac_tests 更新 | ✅ 完成 (M6.5 T1-T5) |
| T5 | NAS 日志补齐 + DETACH 双端 | nas_tests 更新 | ✅ 完成 (M6.5 T1-T5) |
| T6 | UeNode/BsNode + 内存空口 E2E 测试 | attach→data→detach 全绿 | ✅ 完成（e2e_node_tests 5 例；修正先迁态后发送时序） |
| T7 | ue/bs main 重写为薄壳 | 冒烟运行日志正确 | ✅ 完成（stdin 命令 attach/detach/send/status） |
| T8 | sim_channel 重排端口 | 中继转发验证 | ✅ 完成（11001→BS, 21002→UE，中继日志确认） |
| T9 | events.h/PDU_TRACE/log_server 背压+命令通道 | 单测/手工验证 | ✅ 完成（events.h 70 事件全量替换 ev:: 常量；log_server 有界队列背压+_seq+WS→UDP 命令转发；UE 10101/BS 10102 命令口；5s 心跳+PROCESS_EXIT） |
| T10 | LMT 对齐（events.ts/App/store/mock） | tsc 通过 | ✅ 完成（events.ts 镜像+CI 校验；状态推导改用 *_STATE_CHANGE fields；PDU store 按 _seq 增量；遥测卡片取 PHY_CONFIG/RRC_SIB1_RX 真值） |
| T11 | e2e_smoke.py 跨进程冒烟脚本 | 一键跑通退出码 0 | ✅ 完成（直连与 --channel 模式；UDP 命令驱动；15% 丢包下自愈重试） |

---

## 实现备注

- **同步回环时序**：`RachUe`/`NasUe`/`RrcUe` 统一改为"先迁移状态、后发送 PDU"。
  内存管道与跨进程 UDP 回环中，对端响应可能在发送回调内同步到达，
  状态必须先行就位（如 NAS 在 REGISTERING 态才能接受 ATTACH_ACCEPT）。
- **TimerList 绝对时钟**：schedule 的 deadline 锚定到最近一次 tick(now)，
  修复时钟非零起步时定时器立即误触发的问题。
- **RACH PDU 头**：MSG2/MSG3/MSG4 载荷自带 1 字节 RachMsgType 头，
  UeNode/BsNode 解析偏移以此为基准（air frame 的 [type][rnti:2] 头之外）。

## T9–T11 实现备注

- **事件目录**：`stack/common/include/common/events.h` 为单一事实来源，
  全部发射点改用 `ev::<NAME>` 常量；`tools/scripts/check_events_sync.py`
  校验 C++↔events.ts 镜像一致性并接入 CI（.github/workflows/ci.yml）。
- **log_server 背压**：每 WS 客户端有界队列(512)，慢客户端丢旧保新并计数；
  广播消息附服务端单调 `_seq`，LMT PDU store 按其增量处理。
- **命令通道**：WS `{"target":"ue","cmd":"attach"}` → UDP 10101/10102。
  注意 `UdpTransport::recv(timeout=0)` 语义已改为 MSG_DONTWAIT 非阻塞
  （原零值 SO_RCVTIMEO 在 Linux 等于永久阻塞）。
- **UE stdin EOF**：后台运行时管道关闭仅禁用 stdin 通道，进程不再误退出。
- **logger 行刷新**：stdout 为行式遥测流，每次 LOG 后 flush，
  否则全缓冲导致下游（smoke/log_server）长时间看不到事件。
- **丢包自愈（M7.4 前置）**：RACH 在守卫窗口(3s)内塌陷时 UeNode 自动
  重置 RRC 并按 50–200ms 抖动重跑附着（ATTACH_RETRY 事件）；
  e2e_smoke 驱动层对 attach/ping/detach 另做应用级重试
  （TM 承载无重传，单帧丢失由上层重试兜底）。

---

## 更新日志

- `2026-08-24`: 初始计划书创建；深度分析结论并入设计决策
- `2026-08-24`: T6–T8 完成。UeNode/BsNode 落地 + E2E 5 例全绿（91/91 总测试）；
  ue/bs main 薄壳化；sim_channel D3 端口拓扑 + start_demo.sh 接线；
  跨进程 UDP+PHY 冒烟验证 attach→data→detach 打通。
  剩余：T9（事件目录/背压）、T10（LMT 对齐）、T11（冒烟脚本）
- `2026-08-24`: T9–T11 完成，M6.5 全部收尾。事件目录 70 项双端一致 + CI 校验；
  log_server 背压/_seq/命令通道；LMT 状态推导与 PDU store 对齐真实事件流（tsc 通过）；
  e2e_smoke 直连与信道(5%/15% 丢包)模式一键验证通过。
  顺带修复：UdpTransport 零超时阻塞语义、UE stdin EOF 误退出、
  logger stdout 全缓冲延迟、RACH 塌陷自愈重试。

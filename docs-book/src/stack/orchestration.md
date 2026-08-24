# 节点编排层 (UeNode / BsNode)

位置: `stack/core/include/core/ue_node.h`, `stack/core/include/core/bs_node.h`

M6.5 引入的编排层是整个协议栈的"大脑"：每个节点持有全部层实体，
负责空口帧编解码、定时器驱动与跨层联动；进程 main 退化为薄壳
（socket poll + tick + 命令通道），全部协议逻辑可被内存管道单测覆盖。

## 空口帧格式 (PHY 解码后的比特域)

```
[type:1][rnti:2 LE][payload...]

0xA1 MSG1_PRACH   UE→BS  [rach_type][preamble]
0xA2 MSG2_RAR     BS→UE  [rach_type][ra_rnti:2][ta][grant]
0xA3 MSG3_CCCH    UE→BS  [rach_type][ra_rnti:2][CCCH RRC PDU...]
0xA4 MSG4_CR      BS→UE  [rach_type][c_rnti:2]
0xA5 DATA         双向    MAC PDU (LCID 复用)
```

比特域再经 `pack_air_bits()` 加 `[len:16 LE]` 前缀并补齐偶数比特
（QPSK 要求）。RACH PDU 自带 1 字节 `RachMsgType` 头，解析偏移以此为基准。

## MAC LCID 分配

| LCID | 用途 | 链路 |
|------|------|------|
| 0 | CCCH（RRC 建立/释放） | 直通 |
| 1 | NAS DCCH | PDCP+RLC |
| 2 | App DTCH | PDCP+RLC |
| 61 / 62 | SIB1 / MIB 广播（rnti=0xFFFF） | 直通 |
| 63 | PADDING | — |

## UeNode

```cpp
struct UeNodeConfig {
    std::string imsi = "460011234567890";
    mac::RachConfig rach;
    uint32_t rar_window_ms = 250;     // RAR 等待窗
    uint32_t cr_window_ms = 500;      // 竞争解决窗
    uint32_t attach_guard_ms = 3000;  // 附着总守卫
    uint32_t backoff_min_ms = 100;    // RACH 退避抖动
    uint32_t backoff_max_ms = 300;
    uint32_t rng_seed = 42;
};

ue.set_air_send(fn);        // 比特突发出口（测试=内存管道，进程=phy_tx→UDP）
ue.on_air_bits(bits);       // 解码入口
ue.tick(now_ms);            // 显式时钟驱动定时器表
ue.attach(); ue.detach();
ue.send_app_data(payload);
ue.start_traffic(100);      // M7.1 周期回环 + 统计
ue.stop_traffic(); ue.emit_traffic_stats();
```

### 关键内部机制

* **先迁态后发送**：RachUe/NasUe/RrcUe 统一遵循该顺序。同步内存回环中
  对端响应会在发送回调内到达（如 MSG3 发出即收到 MSG4），状态必须先行就位。
* **SIB 门控附着**：`attach()` 在 MIB+SIB1 到达前仅登记请求；
  系统信息到达时自动续跑。
* **守卫自愈**：RACH 在守卫窗口内塌陷 → `ATTACH_RETRY` 抖动后重置
  RRC 并重跑整链；守卫超时 → `ATTACH_ABORT` 全状态机归零。
* **丢失判定**：发出后 3s 未应答的 ping 记 `APP_LOSS`
  （TM 承载无重传，丢失由上层重试兜底）。

## BsNode

```cpp
bs.start_broadcast();       // SIB 立即广播一次 + 每 sib_period_ms(200ms) 周期
bs.on_air_bits(bits); bs.tick(now_ms);
bs.ue_connected(crnti); bs.registered_ue_count();
```

下行 NAS 寻址：维护 `tmsi_to_crnti_` 映射（ATTACH_ACCEPT 时绑定）；
用户面 DTCH 原样回环（ping-pong），RTT 由 UE 侧统计。

## 可观测性

所有跨层收发都经 `core/pdu_trace.h` 的 `trace_pdu()` 打点：
`PDU_TRACE {layer, direction, len, hex(≤48B), brief}` —— LMT 的 PDU 视图数据源。

## 测试

`stack/tests/test_e2e_nodes.cpp`：内存管道直连 UeNode↔BsNode，
覆盖附着、持续回环（5 虚拟分钟 3000 包零丢失）、黑洞恢复、重附着、
守卫超时回收五个场景。

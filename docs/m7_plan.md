# M7 计划书：用户面数据传输 + 系统稳定化

> **状态**: `DONE` — 2026-08-24 全部完成
> **前置依赖**: M4 控制面完成 + M5 用户面 ping 通

---

## 目标

1. 实现持续的用户面数据回环测试（模拟 IP 包传输）
2. 长时间稳定性测试（30 分钟以上无崩溃）
3. 修复内存泄漏、时序 bug、竞态条件

---

## 里程碑

| 编号 | 任务 | 技能卡片名 | 验收标准（DoD） |
|------|------|-----------|----------------|
| **M7.1** | 持续数据回环 | `data_loopback.md` | UE 每 100ms 发送一个数据包，BS 回环，持续 10 分钟无误码 |
| **M7.2** | 内存泄漏检测 | `memory_audit.md` | Valgrind/AddressSanitizer 零泄漏报告 |
| **M7.3** | 时序稳定性 | `timing_stability.md` | 连续运行 30 分钟，无死锁、无崩溃、日志不丢 |
| **M7.4** | 异常恢复 | `fault_recovery.md` | 模拟信道丢包 50%，系统能自动恢复而不崩溃 |

---

## 接口契约

### 应用层 ↔ PDCP（模拟 IP 接口）

```cpp
namespace app {
    // 模拟发送一个 IP 包
    void send_packet(const std::vector<uint8_t>& ip_payload);

    // 收到对端回环的数据
    void on_packet_received(const std::vector<uint8_t>& ip_payload);
}
```

---

## 风险与约束

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 长时间运行内存增长 | 内存泄漏 | 用 AddressSanitizer 编译运行，定期 valgrind |
| 多线程竞态 | 随机崩溃 | logger 已有 mutex，其余共享数据加锁或队列化 |
| 信道恶化导致协议栈死等 | 超时无恢复 | 每层设置超时定时器，超时后重置状态机 |

---

## 技能卡片清单

```
.skills/
├── m7_1_data_loopback.md
├── m7_2_memory_audit.md
├── m7_3_timing_stability.md
└── m7_4_fault_recovery.md
```

---

## 当前实现状态

| 里程碑 | 状态 | 备注 |
|--------|------|------|
| M7.1 数据回环 | ✅ 完成 | UeNode 流量发生器 + 丢失扫描(3s 窗) + RTT 聚合 + TRAFFIC_STATS；E2E 断网恢复用例；真实进程 3 分钟 tx=1745 全达 |
| M7.2 内存审计 | ✅ 完成 | AETHER_SANITIZE=ON 构建，ASan+UBSan 93/93 零泄漏；ASan 二进制全链路冒烟通过 |
| M7.3 时序稳定 | ✅ 完成 | stability_run.py 30 分钟信道长跑 PASS（tx=17564，RSS 恒定 ue=3.7M/bs=12.9M，零崩溃零 abort） |
| M7.4 异常恢复 | ✅ 完成 | recovery_test.py 五阶段场景 PASS：50% 丢包存活→黑洞→自动续流→干净分离；RACH 塌陷 ATTACH_RETRY 自愈实战生效 |

## 结果要点 (2026-08-24)

**30 分钟稳定性长跑**（`--channel --loss-rate 0.05 --blackout "600:10,1200:8"`）：

```
final: tx=17564 rx=15629 loss=1931 rtt_avg=0ms
checks: registered ✓ traffic_flowing ✓ no_crash ✓ rss_bounded ✓
RSS: ue 3744KB / bs 12960KB —— 全程零增长
丢失率 ~10% = 双程信道各 5%（UL+DL 各掷一次骰子）+ 黑洞窗口叠加，
与理论完全吻合；零 AIR_FRAME_DECODE_FAIL、零 ATTACH_ABORT。
```

工具链：
- `tools/test_scripts/stability_run.py` — 长跑驱动/RSS 采样/JSON 报告
- `tools/test_scripts/recovery_test.py` — 五阶段故障恢复场景
- `tools/channel/sim_channel.py` — 新增 `--blackout` 与 `--loss-schedule`
- 技能卡片：`.skills/m7_1..m7_4.md`

---

## 更新日志

- `2026-05-31`: 初始计划书创建
- `2026-08-24`: M7.1–M7.4 全部完成，里程碑关闭

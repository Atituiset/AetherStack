# M7.1 持续数据回环

## 目标
UE 周期性发送用户面数据（模拟 IP 包），BS 回环，长时间运行无误码、可观测。

## 范围
- `UeNode::start_traffic(interval_ms)` / `stop_traffic()` / `traffic_running()`
- 丢失扫描：3s 窗口未应答的 ping 判定丢失（TM 承载无重传），`APP_LOSS` 事件
- RTT 聚合统计：min/max/avg/sample_count，`TRAFFIC_STATS` 每 5s 汇总一次
- UE 命令通道新增 `traffic` / `traffic off` / `stats`

## 接口契约

```cpp
namespace core {
class UeNode {
    void start_traffic(uint32_t interval_ms = 100); // 需已 REGISTERED
    void stop_traffic();                            // detach/abort 自动调用
    uint32_t app_tx_count() const;   // 已发 seq 计数
    uint32_t app_rx_count() const;   // 回环应答数
    uint32_t app_loss_count() const; // 3s 窗口判定丢失数
    int64_t  rtt_min_ms() const; int64_t rtt_max_ms() const;
    int64_t  rtt_avg_ms() const;  // -1 表示无样本
};
}
```

## 事件契约

| 事件 | 字段 |
|------|------|
| TRAFFIC_START | {interval_ms} |
| TRAFFIC_STOP | {} |
| TRAFFIC_STATS | {tx, rx, loss, rtt_min, rtt_max, rtt_avg} |
| APP_LOSS | {seq} |

## DoD
- 内存空口 E2E：100ms×300s 虚拟时钟 → tx==rx==3000、loss==0
- 断网恢复 E2E：黑洞 2s → 丢包被计数、状态保持 CONNECTED、恢复后流量继续
- 真实进程直连 3 分钟：tx=1745 rx=1745 loss=0 rtt_avg=0ms

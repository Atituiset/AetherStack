# Sim Channel 信道模拟器

位置: `tools/channel/sim_channel.py`

## 拓扑 (D3)

流量从专用入口进出信道，节点监听端口保持不变：

```
上行: UE ──► :11001 ──(loss/延迟)──► BS :20002
下行: BS ──► :21002 ──(loss/延迟)──► UE :10001
```

## 参数

```bash
python3 tools/channel/sim_channel.py [options]

--uplink-port 11000x     上行入口端口（默认 11001）
--downlink-port PORT     下行入口端口（默认 21002）
--bs-dest-port PORT      BS 实际监听端口（默认 20002）
--ue-dest-port PORT      UE 实际监听端口（默认 10001）
--loss-rate RATE         基础丢包率 [0,1]（默认 0）
--latency SEC            人为延迟秒（默认 0）
--blackout "600:10"      全断窗口 'start:dur,...'，自启动起算秒
--loss-schedule "30:60:0.5,90:105:1.0"
                         时变丢包曲线 'start:end:rate,...'，覆盖基础丢包率
```

## 示例

```bash
# 5% 丢包常载
python3 tools/channel/sim_channel.py --loss-rate 0.05

# M7.4 场景: 30-60s 50% 丢包, 90-105s 全断
python3 tools/channel/sim_channel.py \
    --loss-schedule "30:60:0.5,90:105:1.0"
```

## 行为细节

* 每个方向独立掷骰子——**往返路径总丢失率 ≈ 1−(1−p)²**（5% 配置实测 ~10%）
* 黑洞/时变窗判定使用单调时钟，与启动时刻对齐
* 主循环带 0.5ms sleep，避免忙转；每行转发/丢弃日志 flush 输出

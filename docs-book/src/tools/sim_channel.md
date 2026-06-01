# Sim Channel 信道模拟器

位置: `tools/channel/sim_channel.py`

## 功能

UDP 中继器，可配置丢包率和延迟，模拟无线信道条件。

## 使用方法

```bash
python tools/channel/sim_channel.py [options]

选项:
  --listen-port PORT    监听 UDP 端口 (默认 10000)
  --dest-host HOST      转发目标地址 (默认 127.0.0.1)
  --dest-port PORT      转发目标端口 (默认 20000)
  --loss-rate RATE      丢包率 0.0-1.0 (默认 0.0)
  --delay-ms MS         单向延迟 (毫秒, 默认 0)
  --seed SEED           随机种子 (默认 42)
```

## 示例

```bash
# 无损中继
python tools/channel/sim_channel.py --listen-port 10001 --dest-port 20001

# 10% 丢包 + 5ms 延迟
python tools/channel/sim_channel.py --loss-rate 0.1 --delay-ms 5

# 双向: UE→BS 和 BS→UE 各一个实例
python tools/channel/sim_channel.py --listen-port 10001 --dest-port 20001 &
python tools/channel/sim_channel.py --listen-port 20002 --dest-port 10002 &
```

## 工作原理

1. 绑定 UDP 监听端口
2. 收到数据报:
   - 以 `loss_rate` 概率丢弃
   - 否则等待 `delay_ms` 后转发到目标
3. 使用 `threading.Timer` 实现延迟
4. 使用固定随机种子保证可重复性

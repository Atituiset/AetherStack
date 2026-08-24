# 构建与运行

## 构建

```bash
make                       # 等价: cmake -S . -B build && cmake --build build
make test                  # 93 个用例 (gtest)
```

### Sanitizer 构建 (M7.2)

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DAETHER_SANITIZE=ON
cmake --build build-asan -j"$(nproc)"
(cd build-asan && ASAN_OPTIONS=detect_leaks=1 ctest)
```

## 一键演示

```bash
./start_demo.sh                        # 交互模式
./start_demo.sh --with-demo            # 无人值守剧本 + LMT 演示横幅
./start_demo.sh --with-demo --loss-rate 0.05
```

启动顺序与就绪探测：构建检查 → venv → log server(9999) → channel →
LMT(:3000) → BS(20002) → UE(10001)；`Ctrl+C` 触发防重入 cleanup。

详见 [演示系统](../demo/demo.md)。

## 手动分组件启动

```bash
.venv/bin/python3 tools/log_server/log_server.py &
python3 tools/channel/sim_channel.py --loss-rate 0.05 &
./build/bin/bs  --log-port 9999 --ue-phy-port 21002 &
./build/bin/ue  --log-port 9999 --bs-phy-port 11001 &
```

## 驱动 UE

stdin（前台）或 UDP 命令口：

```bash
echo attach      | nc -u -w1 127.0.0.1 10101
echo "traffic on" | nc -u -w1 127.0.0.1 10101
```

命令集：`attach` `detach` `send <text>` `traffic on|off` `stats` `status` `quit`。

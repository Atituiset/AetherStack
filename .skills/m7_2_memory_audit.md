# M7.2 内存审计

## 目标
AddressSanitizer + UBSan 构建下全量测试零泄漏、零 UB；ASan 二进制真实进程冒烟通过。

## 方法

```bash
# 构建（独立目录，避免覆盖常规构建）
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DAETHER_SANITIZE=ON
cmake --build build-asan -j"$(nproc)"

# 全量单测（LSAN 默认开启，泄漏即 FAIL）
cd build-asan && ASAN_OPTIONS=detect_leaks=1 ctest --output-on-failure

# 真实进程冒烟：attach -> traffic -> detach 全链路在 ASan 下运行
# （节点进程退出时 LSAN 报告即写入 stderr；无输出 = 干净）
```

## CMake 集成

根 `CMakeLists.txt` 选项 `AETHER_SANITIZE`：
`-fsanitize=address,undefined -fno-omit-frame-pointer -g` + 链接期同参。

## 结果 (2026-08-24)
- 93/93 测试通过，零泄漏报告
- ASan 二进制直连冒烟 attach→traffic(80 包零丢)→detach 通过
- valgrind 未安装（可选交叉验证路径）

## 已知非问题
- logger 的全局 UDP socket 不关闭（进程生命周期全局资源，still-reachable）

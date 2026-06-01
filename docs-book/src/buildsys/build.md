# 构建系统

## 依赖

| 依赖 | 最低版本 | 用途 |
|------|---------|------|
| CMake | 3.16 | 构建系统 |
| C++ 编译器 | C++17 兼容 (GCC 9+ / Clang 10+) | 协议栈编译 |
| Google Test | 1.10+ (自动下载) | 单元测试 |
| Node.js | 16+ | Web LMT |
| Python | 3.8+ | 工具/参考模型 |
| NumPy | 1.20+ | PHY 参考模型 |
| websockets | 10+ | Log Server |

## 构建命令

```bash
# C++ 协议栈 (使用 make 封装)
make                    # Release 构建
make debug              # Debug 构建
make test               # 运行全部测试
make clean              # 清理

# 等效 CMake 命令
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
```

## CMake 结构

```
stack/
├── CMakeLists.txt              # 顶层: 定义子目录
├── common/CMakeLists.txt       # Logger, UDP Transport
├── phy/CMakeLists.txt          # QPSK, OFDM, PHY IO
├── mac/CMakeLists.txt          # MAC PDU, RACH
├── rlc/CMakeLists.txt          # RLC TM
├── pdcp/CMakeLists.txt         # PDCP Entity
├── rrc/CMakeLists.txt          # RRC Messages, Entities
├── nas/CMakeLists.txt          # NAS Messages, Entities
├── app/CMakeLists.txt          # App Layer
└── tests/CMakeLists.txt        # 测试可执行文件 + gtest_discover_tests()
```

## 测试可执行文件

所有测试位于 `./build/bin/`:

| 可执行文件 | 测试数 |
|-----------|--------|
| `phy_tests` | 15 |
| `mac_tests` | 12 |
| `rlc_tests` | 4 |
| `pdcp_tests` | 5 |
| `vertical_tests` | 4 |
| `rrc_tests` | 10 |
| `nas_tests` | 4 |
| `full_attach_tests` | 2 |
| `user_plane_tests` | 4 |
| `stack_tests` | 3 |

总计: **63 个测试**

## Web LMT 构建

```bash
cd lmt
npm install
npm run dev     # 开发服务器 (http://localhost:5173)
npm run build   # 生产构建
```

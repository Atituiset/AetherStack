# M0.1 项目结构骨架

## 目标
创建无线通信系统 MVP 的完整工程目录结构、顶级 CMake/Make 构建文件、各子项目的基础骨架，确保整个项目能一键从源码编译。

## 范围
- 根目录：`wireless-mvp/`
- 三大子系统：C++ 协议栈 (`stack/`)、Python 工具层 (`tools/`)、TypeScript Web LMT (`lmt/`)
- 构建系统：Make/CMake 作为顶层构建管理器，可单独构建任一子系统
- 当前阶段仅要求生成目录、构建骨架、必要的 `main.cpp` / `__init__.py` / 前端空应用，不包含任何协议逻辑

## 具体产出

### 1. 目录树

```
wireless-mvp/
├── Makefile                       # 顶层 Make 构建
├── README.md
├── .skills/                       # 存放所有技能卡片
├── stack/
│   ├── common/                    # 公共组件（日志、定时器、配置等）
│   │   ├── include/
│   │   │   └── common/
│   │   │       └── logger.h       # 日志接口声明
│   │   └── src/
│   │       └── logger.cpp         # 日志实现
│   ├── ue/
│   │   └── src/
│   │       └── main.cpp           # UE 主程序：初始化日志，输出 "UE started"
│   ├── bs/
│   │   └── src/
│   │       └── main.cpp           # BS 主程序：初始化日志，输出 "BS started"
│   └── tests/
│       ├── test_framework.h       # 极简自定义测试框架
│       └── example_test.cpp       # 单元测试文件
├── tools/
│   ├── requirements.txt           # Python 依赖
│   ├── channel/
│   │   ├── __init__.py
│   │   └── sim_channel.py         # 信道仿真进程
│   └── log_server/
│       ├── __init__.py
│       └── log_server.py          # WebSocket 日志聚合服务器
└── lmt/
    ├── package.json
    ├── tsconfig.json
    ├── vite.config.ts
    ├── index.html
    └── src/
        ├── main.tsx
        ├── App.tsx
        └── components/
            └── LogStream.tsx     # 日志流列表组件
```

### 2. 构建要求
- C++17 标准，开启 `-Wall -Wextra`，设置输出目录为 `build/bin/`
- `ue/main.cpp` 和 `bs/main.cpp` 应包含对 `common/logger.h` 的引用并调用其 `init`
- Python 的 `requirements.txt` 至少包含 `numpy` 和 `websockets`
- 前端使用 Vite + React + TypeScript

### 3. 验证标准
- 在项目根目录执行 `make` 编译成功
- 运行 `./build/bin/ue` 和 `./build/bin/bs`
- 运行 `make test` 单元测试通过
- `cd lmt && npm run dev` 启动后，浏览器打开页面显示 LMT

## 依赖的技能卡片
无（起始卡片）

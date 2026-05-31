# user
直接产生三张
# DeepSeek
好的，下面是三张可以直接保存到 `.skills/` 目录下、交给 Claude Code 执行的技能卡片。

---

## 技能卡片 1：M0.1 项目结构

```markdown
# M0.1 项目结构骨架

## 目标
创建无线通信系统 MVP 的完整工程目录结构、顶级 CMake 构建文件、各子项目的基础骨架，确保整个项目能一键从源码编译。

## 范围
- 根目录：`wireless-mvp/`
- 三大子系统：C++ 协议栈 (`stack/`)、Python 工具层 (`tools/`)、TypeScript Web LMT (`lmt/`)
- 构建系统：CMake 作为顶层构建管理器，可单独构建任一子系统
- 当前阶段仅要求生成目录、CMake 骨架、必要的 `main.cpp` / `__init__.py` / 前端空应用，不包含任何协议逻辑

## 具体产出

### 1. 目录树

```
wireless-mvp/
├── CMakeLists.txt                 # 顶层 CMake：包含 stack，提供全局编译选项
├── README.md
├── .gitignore
├── .skills/                       # 存放所有技能卡片
├── stack/
│   ├── CMakeLists.txt             # C++ 协议栈编译
│   ├── common/                    # 公共组件（日志、定时器、配置等）
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── common/
│   │   │       └── logger.h       # 日志接口声明（空壳）
│   │   └── src/
│   │       └── logger.cpp         # 日志实现（留空或仅打印占位）
│   ├── ue/
│   │   ├── CMakeLists.txt
│   │   └── src/
│   │       └── main.cpp           # UE 主程序：初始化日志，输出 "UE started"
│   ├── bs/
│   │   ├── CMakeLists.txt
│   │   └── src/
│   │       └── main.cpp           # BS 主程序：初始化日志，输出 "BS started"
│   └── tests/
│       ├── CMakeLists.txt
│       └── example_test.cpp       # 一个极简的 Google Test 示例
├── tools/
│   ├── requirements.txt           # Python 依赖（至少 websockets, numpy）
│   ├── channel/
│   │   └── __init__.py
│   ├── log_server/
│   │   └── __init__.py
│   └── test_scripts/
│       └── __init__.py
└── lmt/
    ├── package.json
    ├── tsconfig.json
    ├── vite.config.ts
    ├── index.html
    └── src/
        ├── main.tsx
        ├── App.tsx
        └── components/
            └── LogStream.tsx     # 空组件，准备连接 WebSocket
```

### 2. 构建要求
- 顶层 `CMakeLists.txt` 定义 C++17 标准，开启 `-Wall -Wextra`，设置输出目录为 `build/`
- `stack/CMakeLists.txt` 添加 `common`、`ue`、`bs` 三个子目录，并链接 Google Test 到 `tests`
- `ue/main.cpp` 和 `bs/main.cpp` 应包含对 `common/logger.h` 的引用，并调用一个初始化函数（函数体可为空）
- Google Test 示例测试仅包含一个 `EXPECT_EQ(1, 1)`，确保测试框架可用
- Python 的 `requirements.txt` 至少包含 `numpy` 和 `websockets`
- 前端使用 Vite + React + TypeScript，`App.tsx` 中显示 “LMT Ready”，并引入空的 `LogStream` 组件

### 3. 验证标准
- 在项目根目录执行：
  ```bash
  mkdir build && cd build && cmake .. && make
  ```
  编译成功，生成 `build/stack/ue/ue` 和 `build/stack/bs/bs`
- 运行 `./ue` 和 `./bs` 分别打印 “UE started” 和 “BS started” 到控制台
- 在 `build` 目录下运行 `ctest` 或 `make test`，Google Test 示例测试通过
- `cd lmt && npm install && npm run dev` 启动后，浏览器打开页面显示 “LMT Ready”

## 依赖的技能卡片
无（起始卡片）

## 预计工作量
约 1 个 Claude Code 会话（30分钟内）
```

---

## 技能卡片 2：M0.2 统一日志接口

```markdown
# M0.2 统一日志接口（C++ 端）

## 目标
在 C++ 协议栈的 `common/logger` 中实现一个结构化日志系统，能将所有协议层的事件、状态、PDU 信息以 JSON 格式输出到标准输出，并可选通过 UDP 发送给 Python 日志服务。该日志系统将成为整个系统“可观测性”的基石。

## 输入
- 上一里程碑产出的 `common/include/common/logger.h` 和 `common/src/logger.cpp` 空壳
- 项目采用 C++17，编译工具链已就绪

## 产出要求

### 1. 日志 API
`logger.h` 应提供以下公开接口（建议命名空间 `logging`）：

```cpp
namespace logging {
    // 日志级别
    enum class Level { DEBUG, INFO, WARN, ERROR };

    // 初始化：指定模块名（如 “UE”, “BS”），可选远程日志服务器地址及端口
    void init(const std::string& module, 
              const std::string& remote_host = "", 
              uint16_t remote_port = 0);

    // 核心日志函数：级别、事件类型（如 “PHY_RX_OK”, “MAC_STATE_CHANGE”）、
    // 以及一个键值对 map 承载具体字段
    void log(Level level, 
             const std::string& event, 
             const std::map<std::string, std::string>& fields = {});

    // 便捷宏（可选）
    #define LOG_DEBUG(event, ...) logging::log(logging::Level::DEBUG, event, ##__VA_ARGS__)
    // 类似地定义 LOG_INFO, LOG_WARN, LOG_ERROR
}
```

### 2. 日志输出格式
每条日志必须输出为 **单行 JSON**（无换行符），格式如下：
```json
{"timestamp": "2026-05-31T10:30:00.123456", "module": "UE", "level": "INFO", "event": "MAC_STATE_CHANGE", "fields": {"old_state": "IDLE", "new_state": "WAIT_RAR"}}
```
- `timestamp` 使用 ISO 8601 格式，精确到微秒
- `fields` 为可选的键值对字典，若无字段则输出 `{}`

### 3. 远程传输
若初始化时提供了 `remote_host` 和 `remote_port`（且 port > 0），则每条日志在写控制台的同时，通过 UDP 发送到指定地址。UDP 包就是 JSON 字符串本身，无需额外封装。

### 4. 线程安全
`log` 函数必须是线程安全的（可使用 `std::mutex` 保护），因为多协议层可能同时调用。

### 5. 实现文件
`logger.cpp` 需包含：
- 时间戳生成（使用 `<chrono>`）
- JSON 字符串拼接（可手写，不引入第三方库，因为 JSON 结构极其简单）
- UDP 发送（使用 POSIX socket，跨平台可暂时仅支持 Linux）
- 全局 mutex 保证线程安全

### 6. 验证标准
编写一个独立的测试程序 `test_logger.cpp`（或在 UE/BS 的 main 中临时嵌入测试代码），执行以下步骤并确认：

1. 初始化日志，模块名 “TEST”，远程指向 `127.0.0.1:9999`
2. 调用 `LOG_INFO("PHY_RX_OK", {{"snr", "15.2"}});`
3. 控制台打印类似：
   ```json
   {"timestamp":"...","module":"TEST","level":"INFO","event":"PHY_RX_OK","fields":{"snr":"15.2"}}
   ```
4. 用 `nc -ul 9999` 或简单的 Python 脚本监听 UDP 9999 端口，能收到相同 JSON 行
5. 多线程同时调用 `log` 50 次，无崩溃，输出行数完整

测试通过后，将该实现合并到 `common` 库，并更新 `ue/main.cpp` 和 `bs/main.cpp`，使其在启动时调用 `logging::init("UE")` / `logging::init("BS")`，并在 startup 处写一条 `INFO` 事件 `"PROCESS_START"`。

## 依赖
- M0.1 项目结构（`common/include/common/logger.h` 和 `logger.cpp` 已存在占位）

## 后续卡片引用
该技能完成后，所有后续 C++ 协议层卡片都将通过此日志接口输出可观测事件。
```

---

## 技能卡片 3：M0.4 Web 前端骨架

```markdown
# M0.4 Web LMT 骨架

## 目标
搭建基于 Vite + React + TypeScript 的 Web 本地维护终端（LMT）骨架，实现 WebSocket 连接并实时显示从 Python 日志服务推送过来的 JSON 日志流。为后续的拓扑图、状态机视图等高级组件打下基础。

## 前置条件
- Node.js (>= 18) 和 npm 已安装
- 项目目录 `lmt/` 已存在（由 M0.1 创建）

## 产出要求

### 1. 依赖安装与配置
- `package.json` 中应包含以下依赖：
  - `react`, `react-dom`
  - `vite`, `@vitejs/plugin-react`
  - `typescript`, `@types/react`, `@types/react-dom`
- `tsconfig.json` 配置标准 React TS 项目选项
- `vite.config.ts` 基本配置，开发服务器端口可设为 3000

### 2. 入口 HTML
`index.html` 引入 `/src/main.tsx`，页面标题设为 “Wireless LMT”。

### 3. React 应用主组件
`App.tsx` 实现以下功能：
- 顶部标题栏：“Wireless MVP - Local Maintenance Terminal”
- 连接状态指示器：一个圆点（绿色已连接，红色未连接）
- 一个 `LogStream` 组件占位区

### 4. WebSocket 连接管理
创建一个自定义 Hook `useWebSocket`（文件 `src/hooks/useWebSocket.ts`），功能：
- 传入 URL（默认 `ws://localhost:8765`）
- 自动连接，支持断线重连（最多重试 5 次，指数退避）
- 返回 `{ messages: string[], isConnected: boolean }`
- `messages` 是最新接收到的 JSON 字符串数组（最多保留最新 200 条，防止内存溢出）

### 5. 日志流组件
`LogStream.tsx` 组件：
- 使用 `useWebSocket` 获取消息列表
- 以滚动列表形式展示日志，每条日志显示为：
  ```
  [timestamp] [module] level event fields（简略）
  ```
- 支持按 module（UE/BS）和 level 的简单过滤（可留 UI 按钮，逻辑稍后实现）
- 新日志到达时自动滚动到底部，可暂停自动滚动

### 6. 临时模拟数据
在没有后端时，开发模式允许应用启动时自动生成几条模拟 JSON 日志并追加到列表，以便看到 UI 效果。通过一个 prop `mockMode` 控制，默认为 false，可在 `App.tsx` 中手动开启。

### 7. 验证标准
- 进入 `lmt/` 目录，执行 `npm install && npm run dev`
- 浏览器打开 `http://localhost:3000`，页面显示标题、红色连接状态灯、空的日志列表
- 若使用模拟模式，能看到预设的几条日志滚动出现
- 启动一个简单的 Python WebSocket 服务（例如用 `websockets` 库）向 `ws://localhost:8765` 推送测试 JSON，前端连接灯变绿，日志实时出现，且过滤器可工作（至少 UI 按钮存在）

## 交付物
所有源代码文件填入 `lmt/` 目录。完成后，前端应用可独立启动并与 Python WebSocket 服务交互。

## 依赖
- M0.1 已建立 `lmt/` 目录结构

## 后续
后续卡片将在此基础上增加拓扑图、MSC 图等可视化组件。
```

---

**以上三张卡片已就绪。** 你可以将它们分别保存为 `.skills/m0_1_project_structure.md`、`.skills/m0_2_unified_logger.md`、`.skills/m0_4_web_lmt_skeleton.md`，然后通过 Claude Code 引用对应卡片开始实现。建议从 M0.1 开始，因为 M0.2 和 M0.4 的部分文件结构依赖它。完成后你会拥有一个能编译、能打日志、能在网页上看到日志流的完整骨架。

需要我继续生成接下来的物理层技能卡片吗？
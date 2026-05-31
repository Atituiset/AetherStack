# M0.2 统一日志接口（C++ 端）

## 目标
在 C++ 协议栈的 `common/logger` 中实现一个结构化日志系统，能将所有协议层的事件、状态、PDU 信息以 JSON 格式输出到标准输出，并可选通过 UDP 发送给 Python 日志服务。该日志系统将成为整个系统“可观测性”的基石。

## 输入
- `common/include/common/logger.h` 和 `common/src/logger.cpp`
- 项目采用 C++17，编译工具链已就绪

## 产出要求

### 1. 日志 API
`logger.h` 应提供以下公开接口（建议命名空间 `logging`）：

```cpp
namespace logging {
    enum class Level { DEBUG, INFO, WARN, ERROR };

    void init(const std::string& module, 
              const std::string& remote_host = "", 
              uint16_t remote_port = 0);

    void log(Level level, 
             const std::string& event, 
             const std::map<std::string, std::string>& fields = {});

    #define LOG_DEBUG(event, ...) logging::log(logging::Level::DEBUG, event, ##__VA_ARGS__)
    #define LOG_INFO(event, ...)  logging::log(logging::Level::INFO, event, ##__VA_ARGS__)
    #define LOG_WARN(event, ...)  logging::log(logging::Level::WARN, event, ##__VA_ARGS__)
    #define LOG_ERROR(event, ...) logging::log(logging::Level::ERROR, event, ##__VA_ARGS__)
}
```

### 2. 日志输出格式
每条日志必须输出为 **单行 JSON**（无换行符），格式如下：
```json
{"timestamp": "2026-05-31T10:30:00.123456Z", "module": "UE", "level": "INFO", "event": "MAC_STATE_CHANGE", "fields": {"old_state": "IDLE", "new_state": "WAIT_RAR"}}
```
- `timestamp` 使用 ISO 8601 UTC 格式，精确到微秒
- `fields` 为可选的键值对字典，若无字段则输出 `{}`

### 3. 远程传输
若初始化时提供了 `remote_host` 和 `remote_port`（且 port > 0），则每条日志在写控制台的同时，通过 UDP 发送到指定地址。

### 4. 线程安全
`log` 函数必须是线程安全的（使用 `std::mutex` 保护）。

### 5. 验证标准
1. 初始化日志指向 `127.0.0.1:9999`
2. 调用 `LOG_INFO("PHY_RX_OK", {{"snr", "15.2"}});`
3. 控制台与 UDP 均能收到相同 JSON 行
4. 多线程同时调用安全无崩溃

## 依赖
- M0.1 项目结构

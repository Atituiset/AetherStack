# 公共基础设施

## Logger (`logging` 命名空间)

位置: `stack/common/include/common/logger.h`

### 功能

- 单行 JSON 输出（每次写入即 flush——stdout 就是实时遥测流）: `{"timestamp":"...","module":"UE","level":"INFO","event":"MAC_RACH_MSG1","fields":{...}}`
- ISO 8601 时间戳 (微秒精度)
- 线程安全 (内部 `std::mutex`)
- 可选 UDP 远程日志: 发送到 Log Server (port 9999)
- 4 个便利宏: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`

### 接口

```cpp
namespace logging {
  enum class Level { DEBUG, INFO, WARN, ERROR };

  void init(const std::string& module_name,
            const std::string& remote_host = "",
            uint16_t remote_port = 0);

  void log(Level level,
           const std::string& event,
           const std::map<std::string, std::string>& fields = {});

  #define LOG_DEBUG(event, ...)  ::logging::log(::logging::Level::DEBUG, event, ##__VA_ARGS__)
  #define LOG_INFO(event, ...)   ::logging::log(::logging::Level::INFO, event, ##__VA_ARGS__)
  #define LOG_WARN(event, ...)   ::logging::log(::logging::Level::WARN, event, ##__VA_ARGS__)
  #define LOG_ERROR(event, ...)  ::logging::log(::logging::Level::ERROR, event, ##__VA_ARGS__)
}
```

### 使用示例

```cpp
logging::init("UE", "127.0.0.1", 9999);
LOG_INFO("PROCESS_START", {{"msg", "UE starting up"}});
LOG_WARN("RACH_RAR_TIMEOUT", {{"retry", "2"}});
```

---

## 事件目录 (`common/events.h`, M6.5 D5)

结构化日志的事件名**单一事实来源**——75 个常量按层分组并注释字段契约，
所有发射点使用 `ev::<NAME>` 引用（禁止裸字符串）。镜像文件
`lmt/src/events.ts` 由 CI 脚本 `tools/scripts/check_events_sync.py`
强制一致。

```cpp
#include "common/events.h"
LOG_INFO(ev::RACH_SUCCESS, {{"c_rnti", "1"}});   // ✓
LOG_INFO("RACH_SUCCESS", ...);                   // ✗ CI 会抓出裸字面量漂移
```

---

## UDP Transport (`transport` 命名空间)

位置: `stack/common/include/common/udp_transport.h`

### 功能

- POSIX socket 封装
- 支持 `bind()` + `set_dest()` + `send()` + `recv(timeout)`
- 用于 UE/BS 的 PHY IQ 样本交换

### 接口

```cpp
namespace transport {
  class UdpTransport {
  public:
    UdpTransport();
    ~UdpTransport();

    bool bind(const std::string& local_addr, uint16_t local_port);
    bool set_dest(const std::string& dest_addr, uint16_t dest_port);
    bool send(const uint8_t* data, size_t len);
    bool send(const std::vector<uint8_t>& data);
    int recv(uint8_t* buf, size_t buf_len, int timeout_ms = 1000);
    int fd() const;
  };
}
```

### 典型用法

```cpp
transport::UdpTransport sock;
sock.bind("0.0.0.0", 10001);
sock.set_dest("127.0.0.1", 20002);
sock.send(iq_bytes);

uint8_t buf[65536];
int len = sock.recv(buf, sizeof(buf), 500);
```

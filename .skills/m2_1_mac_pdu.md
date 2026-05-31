# M2.1 MAC PDU 编解码

## 目标
实现 MAC PDU 的组包（build_pdu）与拆包（parse_pdu），支持简化的子头格式（R|F|LCID 字节 + 可变长度 L 字段），为 RACH 和后续调度提供数据帧封装基础。

## 范围
- 子头格式：1 字节 [R|F|LCID]，F=0 时 L 占 1 字节，F=1 时 L 占 2 字节
- LCID：0=CCCH, 1~32=逻辑信道, 63=Padding
- 多 SDU 拼接：一个 PDU 可包含多个 (LCID, SDU) 对

## 接口契约

```cpp
namespace mac {
constexpr uint8_t LCID_CCCH = 0;
constexpr uint8_t LCID_PADDING = 63;

// 构建 MAC PDU：多个 (LCID, SDU) 对 → 字节流
std::vector<uint8_t> build_pdu(
    const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& sdus);

// 解析 MAC PDU：字节流 → 多个 (LCID, SDU) 对
std::vector<std::pair<uint8_t, std::vector<uint8_t>>>
    parse_pdu(const std::vector<uint8_t>& pdu);
}
```

## 子头格式
```
Byte 0: [R|F|LCID6..0]
  R = 保留 (0)
  F = 0 → L 占 1 字节 (0~255)
  F = 1 → L 占 2 字节 (0~65535)
  LCID = 逻辑信道 ID

F=0: [R|F|LCID] [L8] [SDU...]
F=1: [R|F|LCID] [L15..8] [L7..0] [SDU...]
```

## 验证标准
1. 单 SDU round-trip：编码 → 解码，LCID 和 payload 不变
2. 多 SDU round-trip：3 个不同 LCID 的 SDU 拼接后可正确拆包
3. 大 SDU (len>255)：自动使用 F=1 的 2 字节 L 字段
4. CCCH (LCID=0) 特殊通道 round-trip

## 依赖
- M0.1 项目结构
- M0.2 统一日志

## 产出文件
- `stack/mac/include/mac/mac_pdu.h`
- `stack/mac/src/mac_pdu.cpp`
- `stack/mac/tests/test_mac.cpp`（PDU 部分）

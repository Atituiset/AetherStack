# M4.1 RRC 系统消息

## 目标
实现 BS 周期性广播精简 MIB/SIB1，UE 接收并解析。这是 UE 小区搜索后的第一步——获取系统信息。

## 范围
- MIB（Master Information Block）：精简版，只含下行带宽、PHICH 配置、SFN
- SIB1（System Information Block 1）：精简版，含 PLMN ID、TAC、小区 ID
- BS 广播：通过 BCCH 逻辑信道周期性发送
- UE 接收：解析 MIB/SIB1，日志打印内容

## 接口契约

```cpp
namespace rrc {
struct Mib {
    uint16_t sfn = 0;
    uint8_t dl_bandwidth = 50;  // MHz / 5 → 10MHz
    uint8_t phich_config = 0;
    std::vector<uint8_t> encode() const;
    static Mib decode(const std::vector<uint8_t>& data);
};

struct Sib1 {
    std::string plmn_id = "46001";
    uint16_t tac = 1;
    uint16_t cell_id = 1;
    std::vector<uint8_t> encode() const;
    static Sib1 decode(const std::vector<uint8_t>& data);
};

// BS 侧：生成系统消息
Mib generate_mib(uint16_t sfn);
Sib1 generate_sib1();
}
```

## 验证标准
1. MIB encode → decode round-trip 字段不变
2. SIB1 encode → decode round-trip 字段不变
3. BS 生成 MIB/SIB1 并通过 MAC BCCH 信道发送

## 依赖
- M2 MAC PDU
- M3 PDCP 透传

## 产出文件
- `stack/rrc/include/rrc/rrc_messages.h`
- `stack/rrc/src/rrc_messages.cpp`
- `stack/rrc/tests/test_rrc_state.cpp`（系统消息部分）

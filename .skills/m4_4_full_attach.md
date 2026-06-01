# M4.4 初始附着全流程

## 目标
将 M1~M4 的所有模块串联，实现从 UE 冷启动到 NAS Attach Complete 的完整控制面流程。一键启动验证。

## 范围
- 完整流程：小区搜索（简化）→ SIB 接收 → RACH → RRC 连接 → NAS 附着
- 单进程集成测试，各模块通过回调串联
- 日志记录每一步状态转换

## 完整流程
```
UE 冷启动
→ RRC UE 接收 MIB/SIB1（BS 广播）
→ MAC RACH 4步握手（MSG1→MSG4）
→ RRC 连接建立（SetupRequest→Setup→SetupComplete）
→ NAS 附着（AttachRequest→AttachAccept）
→ 状态: NAS REGISTERED
```

## 验证标准
1. 从 IDLE 到 REGISTERED 完整流程不崩溃
2. 每一步状态转换正确
3. BS 侧有完整的 UE 上下文（C-RNTI + TMSI）
4. 日志显示完整的 MSC 消息序列

## 依赖
- M4.1 RRC SIB
- M4.2 RRC 连接
- M4.3 NAS 附着
- M2 RACH
- M1 PHY

## 产出文件
- `stack/tests/test_full_attach.cpp`

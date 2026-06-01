# 技能卡片系统

位置: `.skills/` 目录

## 概念

每个里程碑对应一组技能卡片 (Markdown 文件)，定义了该里程碑的目标、范围、接口契约和验证标准。技能卡片既是开发规范，也是验收依据。

## 格式

每张技能卡片遵循 M0 定义的模板:

```markdown
# <Skill Name>

## 目标
<该技能要达成的目标>

## 范围
<涉及的功能范围和边界>

## 接口契约
<函数签名、数据格式、协议约定>

## 验证标准
<如何验证该技能正确实现>

## 依赖
<依赖的前置技能/里程碑>

## 输出文件
<该技能产出的源文件列表>
```

## 卡片清单

### M0 (7 张)

| 卡片 | 文件 | 覆盖 |
|------|------|------|
| cmake_build | `M0_cmake_build.md` | CMake 构建系统 |
| logger | `M0_logger.md` | JSON 远程日志 |
| udp_transport | `M0_udp_transport.md` | POSIX UDP 封装 |
| channel_sim | `M0_channel_sim.md` | Python 信道模拟 |
| web_lmt | `M0_web_lmt.md` | React LMT 脚手架 |
| project_skeleton | `M0_project_skeleton.md` | 项目结构 |
| git_workflow | `M0_git_workflow.md` | Git 标签工作流 |

### M2 (4 张)

| 卡片 | 覆盖 |
|------|------|
| mac_pdu | MAC PDU 编解码 |
| rach_fsm | RACH UE 状态机 |
| rach_bs | RACH BS 处理器 |
| rach_e2e | RACH 端到端测试 |

### M3 (3 张)

| 卡片 | 覆盖 |
|------|------|
| rlc_tm | RLC 透明模式 |
| pdcp_entity | PDCP 实体 |
| vertical_test | 垂直集成测试 |

### M4 (4 张)

| 卡片 | 覆盖 |
|------|------|
| rrc_messages | RRC 消息编解码 |
| rrc_entities | RRC UE/BS 实体 |
| nas_messages | NAS 消息编解码 |
| nas_entities | NAS UE/BS 实体 |

### M5 (3 张)

| 卡片 | 覆盖 |
|------|------|
| app_layer | 用户面应用层 |
| user_plane_test | 用户面测试 |
| analysis_scripts | Python 分析脚本 |

### M6 (4 张)

| 卡片 | 覆盖 |
|------|------|
| topology_canvas | SVG 拓扑图 |
| fsm_viewer | 状态机可视化 |
| msc_diagram | 消息序列图 |
| pdu_detail | PDU 详情查看 |

**总计: 25 张技能卡片**

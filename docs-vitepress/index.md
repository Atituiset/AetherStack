---
layout: home

hero:
  name: AetherStack
  text: 技术文档
  tagline: 从零构建的 5G NR 无线协议栈 MVP —— 覆盖 PHY 到 NAS 全七层，可运行、可观测、可调试
  actions:
    - theme: brand
      text: 开始阅读
      link: /intro
    - theme: alt
      text: 项目概览
      link: /overview/project
    - theme: alt
      text: 协议栈参考
      link: /stack/reference

features:
  - title: 协议栈全七层
    details: PHY / MAC / RLC / PDCP / RRC / NAS / App 完整实现，每层附消息格式、实体设计与测试文档。
    link: /stack/reference
  - title: PHY 物理层
    details: QPSK 调制解调、OFDM 收发、PHY I/O 序列化与 Python/NumPy 黄金参考模型。
    link: /phy/phy
  - title: RRC / NAS 信令
    details: RRC 连接建立与 NAS 附着的消息格式、UE/BS 双侧实体与逐层测试。
    link: /rrc/rrc
  - title: 集成测试
    details: 全链路垂直测试、完整附着流程与用户面 Ping-Pong 的端到端验证。
    link: /integration/integration
  - title: Web LMT 监控终端
    details: React 18 + TypeScript 实时仪表盘：拓扑、FSM、MSC、PDU 可视化。
    link: /lmt/lmt
  - title: Python 工具链
    details: 日志服务器、信道模拟器、验证工具集与分析脚本（MSC / PDU / RTT）。
    link: /tools/tools
  - title: 构建与里程碑
    details: CMake + Google Test 构建体系、一键演示脚本与 M0–M15 逐阶段历程沉淀。
    link: /buildsys/build
---

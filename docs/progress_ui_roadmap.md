# UI 改造路线图进度记录（会话快照）

> 记录时间：P8 进行中（后端 agent 因额度中断，待恢复）。本文件是会话级快照，里程碑设计细节见各 `m*_plan.md`。

## 已完成（全部经过实机验证）

| 阶段 | 内容 | 验证 |
|------|------|------|
| P0 | 修复 UL 用户面拥塞崩溃且不恢复（PHY CPU 悬崖 / HARQ 预算 / RLC AM 死锁三连环）；StoryView v1 演示视图 | 148/148 |
| P1 | UE3 接入：三 UE 端到端（脚本/命令路由/信道 fan-out + 前端三机布局、选目标拨号） | 实测三 UE 通话+通话中发消息 |
| P2 | SIP-lite 呼叫信令（INVITE/180/200/ACK/BYE/CANCEL/486/603）+ 前端真实振铃、接听/拒接/占线剧情 | 155/155 |
| P3 | QoS 专有承载（sig QCI5 > voice QCI1 > video QCI2 > BE QCI9，严格优先级 + 25% BE 保底）+ 前端并发通话/QoS 统计分离/承载 pips | 161/161，确定性竞争测试证明语音保护 |
| P4 | 三方会议（BS 语音桥，conf_id 对话组）+ 前端会议场景（加入/离开/结束字幕、桥接扇出动画） | 164/164 + npm test 42/42 |
| P5 | 链路自适应 CQI→MCS（16QAM）+ 上行功控（修复增益敏感 SNR 估计导致的发散）+ 信道模拟器并行化 + 前端信号格/MCS 徽章/发射功率 | 167/167 + 54/54 |
| P6 | RRC Inactive/Resume（resume 比 attach 省 43%）+ 寻呼唤醒 + 前端锁屏/唤醒/寻呼剧情 | 172/172 + 66/66 |
| P7 | 5G-AKA（RAND/AUTN/SQN/AUTS，KASME 喂 PDCP ChaCha20）+ 前端鉴权节拍/失败剧情 | 177/177 + 74/74 |

## 进行中

- **P8 双基站移动性切换**：前端已完成（双塔场景、切换动画、85/85 断言，兼容 M14 旧事件族）；后端中断前状态未知——恢复后先 `git status` 评估半成品，目标：第二 BS 进程（PHY 20003/cmd 10106）、信道双小区共享介质、`move` 命令、优先 Xn-lite 上下文转移切换、通话中切换不中断。

## 关键运维注意事项

- 跑 `start_demo.sh` 必须重定向输出到文件（媒体速率下日志量会撑爆 16 MiB 捕获上限）。
- UE 命令端口：ue1=10101, ue2=10103, ue3=10104, bs=10102；log_server 命令路由 `CMD_PORTS`。
- 事件目录双端镜像：`stack/common/include/common/events.h` ↔ `lmt/src/events.ts`，改动后必须跑 `python3 tools/scripts/check_events_sync.py`。
- 前端派生逻辑有 `lmt/` 下的 npm test 套件（`cd lmt && npm test`，当前 85 断言）。
- 所有工作**未提交 git**（工作树改动 = P0 起全部内容），如需提交需用户确认。

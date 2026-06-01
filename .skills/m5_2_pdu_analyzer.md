# M5.2 PDU 十六进制分析器

## 目标
Python 脚本对每层 PDU 进行 hex dump 并逐层解包：MAC→RLC→PDCP→RRC/NAS，打印每层头部和 payload 摘要。

## 范围
- 输入：PDU_TRACE 日志中的 hex 字段
- 解包逻辑：MAC 子头 → RLC TM → PDCP 头 → RRC/NAS TLV
- 输出：彩色终端 hex dump 或 JSON 结构化解析结果

## 接口
```bash
python tools/scripts/pdu_analyzer.py < pdu_trace.log
```

## 验证标准
1. 输入含 MAC PDU 的 PDU_TRACE，输出子头 LCID + payload 解析
2. 递归解包到最内层（NAS/APP）
3. 非法 PDU 不崩溃，标记 "PARSE_ERROR"

## 依赖
- M2.1 MAC PDU 格式
- M3.1 RLC TM 格式
- M3.2 PDCP 头格式

## 产出文件
- `tools/scripts/pdu_analyzer.py`

# M0.4 Web LMT 骨架

## 目标
搭建基于 Vite + React + TypeScript 的 Web 本地维护终端（LMT）骨架，实现 WebSocket 连接并实时显示从 Python 日志服务推送过来的 JSON 日志流。为后续的拓扑图、状态机视图等高级组件打下基础。

## 前置条件
- Node.js (>= 18) 和 npm 已安装
- 项目目录 `lmt/` 已存在

## 产出要求

### 1. React 应用主组件
`App.tsx` 实现以下功能：
- 顶部标题栏：“Wireless MVP - Local Maintenance Terminal”
- 连接状态指示器：一个圆点（绿色已连接，红色未连接）
- 一个 `LogStream` 组件占位区

### 2. WebSocket 连接管理
创建自定义 Hook `useWebSocket`：
- 自动连接，支持断线重连（最多重试 5 次，指数退避）
- 返回 `{ messages: string[], isConnected: boolean }`
- `messages` 数组最多保留最新 200 条，防止内存溢出

### 3. 日志流组件
`LogStream.tsx` 组件：
- 使用 `useWebSocket` 获取消息列表并以滚动列表形式展示
- 新日志到达时自动滚动到底部，可暂停自动滚动
- 过滤功能：支持按 module (UE/BS) 和 level 过滤

### 4. 验证标准
- 进入 `lmt/` 目录，执行 `npm run dev` 能够成功启动
- 浏览器打开 `http://localhost:3000` 显示主界面
- 模拟模式下可以自动滚动虚拟数据
- 连接后端 WebSocket 后，日志流正常更新

## 依赖
- M0.1 项目结构

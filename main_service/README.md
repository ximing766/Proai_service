# ProAI Main Service 核心文档

`main_service` 是运行在 Linux 主控板上的核心调度服务

## 目录

- [1. 架构总览](#1-架构总览)
- [2. 当前代码实现状态](#2-当前代码实现状态)
- [3. 线程与并发模型](#3-线程与并发模型)
- [4. 核心消息流（下行与上行）](#4-核心消息流下行与上行)
- [5. 关键目录与文件说明](#5-关键目录与文件说明)
- [6. 编译与部署](#6-编译与部署)
- [7. 运行与启动参数（CLI）](#7-运行与启动参数cli)
- [8. 日志系统](#8-日志系统)
- [9. MCU 模拟器使用](#9-mcu-模拟器使用)
- [10. Tongqu SDK 与工具链核心要点](#10-tongqu-sdk-与工具链核心要点)
- [11. 常见问题与排查](#11-常见问题与排查)

## 1. 架构总览

架构图如下（仓库根目录）：

![系统架构](../../系统架构.png)

## 3. 线程与并发模型

### 3.1 线程职责

- 主线程：
  - 消费系统队列
  - 作为串口写入唯一出口
  - 执行周期任务（心跳、测试文本发送）
- `uart_rx_thread`：
  - 串口字节流读取
  - Tuya 帧解析
  - MCU 上行消息处理（当前日志+分发）
  - 自动重连（串口不可用时循环重试）

### 3.2 队列与 Mutex 的关系

- 队列用于“异步解耦 + 顺序调度 + 缓冲突发流量”
- Mutex 用于保护“队列内部数据结构”的并发安全
- 串口发送通过主线程统一执行，避免多线程并发写串口

## 4. 核心消息流（下行与上行）

### 4.1 下行控制流（多端 -> MCU）

- 来源：AI 回调 / 涂鸦回调 / 离线语音回调 / 定时任务
- 处理：打包为 `SystemMsg`，`msg_queue_push()`
- 执行：主线程 `msg_queue_pop()` -> `tuya_send_cmd()` -> 串口下发

### 4.2 上行状态流（MCU -> 云端）

- 来源：`uart_rx_thread` 解析到 MCU 上报帧
- 当前：日志打印与基础分发
- 规划：按命令类型上报 AI 平台或涂鸦云平台


## 6. 编译与部署

### 6.2 自动部署（deploy.sh）

```bash
cd /home/xm/proai/Proai_service/main_service

# 普通部署
./deploy.sh

# strip 后再部署
./deploy.sh --strip
```

## 7. 运行与启动参数（CLI）

程序入口：

```bash
./proai_service [options]
```

参数说明：

- `-s`：仅终端输出日志（不写文件）
- `-v <level>`：设置日志级别
  - `0=DEBUG`
  - `1=INFO`（默认）
  - `2=WARN`
  - `3=ERROR`
  - `4=NONE`
- `-h`：显示帮助

示例：

```bash
# 默认：文件日志 + INFO
./proai_service

# 仅终端输出 + DEBUG
./proai_service -s -v 0

# 只看错误
./proai_service -v 3
```

常用环境命令：

```bash
date -u -s "2026-05-06 14:50:00"
export TZ=CST-8
ntpd -gq -p pool.ntp.org

export AGENT_WS_URL="wss://tongqu.zworker.online/ws/v1/chat"
export AGENT_DEVICE_ID="0001"
export AGENT_DEVICE_SECRET="K2JJTF9SWL4NWWK28DRP7W9YAX4FSRAQ"

ssh root@192.168.1.7
scp ca-certificates.crt root@192.168.1.7:/root/workspace/proai/
```



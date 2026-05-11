# ProAI Main Service 核心文档

`main_service` 是运行在 Linux 主控板上的核心调度服务，负责把三类能力串联起来：

- 云端能力：AI 平台（Tongqu SDK
- 本地能力：与兔子控制板 MCU 的串口协议交互
- 媒体能力：语音输入/输出（当前为桩接口，待 SDK/驱动对接）

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

## 2. 当前代码实现状态

- 已实现：
  - 主线程队列调度与统一串口下发
  - UART 后台线程自动重连（模拟器未启动时服务不退出）
  - Tongqu SDK 初始化、文本发送、IoT descriptor 注册
  - 可选 `strip` 构建与部署流程
- 待完善：
  - `tool_calls` 到 `SystemMsg` 的完整结构化映射
  - MCU 上报统一转发到 AI/Tuya 的适配层
  - 音频模块从桩切换到真实驱动/SDK

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

### 4.3 音频流（独立旁路）

- AI 下行音频：`on_audio()` 直接调用 `audio_module_play()`
- 不进入主控指令队列，避免音频流阻塞控制指令

## 5. 关键目录与文件说明

- `main_service/main.c`：主流程、参数解析、队列消费、串口线程管理
- `main_service/src/queue.c`：线程安全队列实现
- `main_service/inc/queue.h`：消息类型与队列接口定义
- `main_service/src/cloud_llm.c`：AI 平台连接、回调、文本/JSON 发送
- `main_service/src/cloud_tuya.c`：涂鸦平台桩实现（待 SDK 接入）
- `main_service/src/audio_module.c`：音频模块桩实现（待音频 SDK 接入）
- `main_service/src/log.c`：日志系统（级别过滤、文件轮转、毫秒时间戳）
- `main_service/inc/log.h`：日志级别与宏定义
- `mcutools/rabbit_mcu_sim.py`：MCU 串口模拟器
- `main_service/build.sh`：交叉编译脚本（支持 `--strip`）
- `main_service/deploy.sh`：编译并 scp 部署脚本（支持 `--strip`）

## 6. 编译与部署

### 6.1 交叉编译（build.sh）

```bash
cd /home/xm/proai/Proai_service/main_service

# 普通构建（保留符号，便于调试）
./build.sh

# 产物 strip（更小体积，便于发布）
./build.sh --strip
```

### 6.2 自动部署（deploy.sh）

```bash
cd /home/xm/proai/Proai_service/main_service

# 普通部署
./deploy.sh

# strip 后再部署
./deploy.sh --strip
```

### 6.3 发给他人运行的最小打包清单

至少包含：

- `proai_service`（建议 strip 后版本）
- `ca-certificates.crt`
- `rockchip830_runtime_bundle/lib/`（运行时 `.so` 依赖）

推荐附带：

- `rabbit_mcu_sim.py`（对方没有真实 MCU 时联调）
- `run.sh`（封装 `LD_LIBRARY_PATH` 与启动参数）

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

## 8. MCU 模拟器使用

### 8.1 启动模拟器

```bash
cd /home/xm/proai/Proai_service/mcutools
python3 rabbit_mcu_sim.py
```

模拟器会创建：

- 符号链接：`/tmp/ttyModule`
- 主服务默认连接该虚拟串口

### 8.2 启停管理（目标板）

```bash
/etc/init.d/S99rabbit_mcu_sim start
/etc/init.d/S99rabbit_mcu_sim stop
/etc/init.d/S99rabbit_mcu_sim restart
ps | grep rabbit_mcu_sim.py
```

## 9. Tongqu SDK 与工具链核心要点

### 9.1 构建期目录要求

`CMakeLists.txt` 默认依赖以下目录：

- `../tongqu-sdk/agent_linux_sdk_rockchip830`
- `../tongqu-sdk/rockchip830_runtime_bundle/lib`

如果目录不存在，请先解压 SDK 资产并按上述路径组织。

### 9.2 运行时依赖

链接库包括：

- `agent_sdk`
- `websockets`
- `curl`
- `pthread`
- `m`
- `rt`
- `dl`

目标板运行前建议设置：

```bash
export LD_LIBRARY_PATH=/root/workspace/proai/rockchip830_runtime_bundle/lib:$LD_LIBRARY_PATH
```

### 9.3 证书与 AI 连接

`cloud_llm.c` 当前默认使用：

- `/root/workspace/proai/ca-certificates.crt`

请确保目标板路径与文件存在，否则会出现 HTTPS/WSS 连接异常。

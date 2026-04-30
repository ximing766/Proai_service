# ProAI Main Service 核心文档

`main_service` 是运行在 Linux 主控板上的核心调度服务，负责把三类能力串联起来：

- 云端能力：AI 平台（Tongqu SDK）与未来的涂鸦云 SDK
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

架构要点：

- 多输入：AI 云下行、涂鸦云下行、离线语音下行（预留）
- 多输出：MCU 控制指令、云端状态上报、语音播放输出
- 控制流和音频流分离：
  - 控制流进入系统队列统一调度
  - 音频流走旁路，不进入主控指令队列，防止阻塞

## 3. 线程与并发模型

### 3.1 线程职责

- 主线程：
  - 消费系统队列
  - 作为串口写入唯一出口
  - 执行周期任务（心跳、测试文本发送）
- `uart_rx_thread`：
  - 串口字节流读取
  - Tuya 帧解析
  - MCU 上行消息处理（未来可转发 AI/涂鸦云）
- SDK 内部线程（由第三方 SDK 管理）：
  - AI WebSocket 收发、回调触发
  - 未来涂鸦 SDK 事件循环

### 3.2 队列与 Mutex 的关系

- 队列用于“异步解耦 + 顺序调度 + 缓冲突发流量”
- Mutex 用于保护“队列内部数据结构”的并发安全
- 如果所有串口发送都通过“主线程消费队列”执行，则串口写不需要额外业务锁

## 4. 核心消息流（下行与上行）

### 4.1 下行控制流（多端 -> MCU）

- 来源：AI 回调 / 涂鸦回调 / 离线语音回调 / 定时任务
- 处理：打包为 `SystemMsg`，`msg_queue_push()`
- 执行：主线程 `msg_queue_pop()` -> `tuya_send_cmd()` -> 串口下发

### 4.2 上行状态流（MCU -> 云端）

- 来源：`uart_rx_thread` 解析到 MCU 上报帧
- 当前：日志打印与基础状态处理
- 规划：根据命令类型上报给 AI 平台或涂鸦云平台

### 4.3 音频流（独立旁路）

- AI 下行音频：`on_audio()` 直接调用 `audio_module_play()`
- 不进入主控指令队列，避免大流量音频阻塞控制指令

## 5. 关键目录与文件说明

- `main_service/main.c`：主流程、参数解析、队列消费、串口线程管理
- `main_service/src/queue.c`：线程安全队列实现
- `main_service/inc/queue.h`：消息类型与队列接口定义
- `main_service/src/cloud_llm.c`：AI 平台连接、回调、文本/JSON发送
- `main_service/src/cloud_tuya.c`：涂鸦平台桩实现（待 SDK 接入）
- `main_service/src/audio_module.c`：音频模块桩实现（待音频 SDK 接入）
- `main_service/src/log.c`：日志系统（级别过滤、文件轮转、毫秒时间戳）
- `main_service/inc/log.h`：日志级别与宏定义
- `mcutools/rabbit_mcu_sim.py`：MCU 串口模拟器
- `main_service/build.sh`：交叉编译脚本
- `main_service/deploy.sh`：编译并 scp 部署到目标板脚本

## 6. 编译与部署

### 6.2 交叉编译（build.sh）

```bash
cd /home/xm/proai/Proai_service/main_service
bash build.sh
./build.sh
```

### 6.3 自动部署（deploy.sh）

```bash
cd /home/xm/proai/Proai_service/main_service
bash deploy.sh
./deploy.sh
```

## 7. 运行与启动参数（CLI）

程序入口支持以下参数：

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

## 8. 日志系统

- 目录：`main_service/log/`
- 文件名：`proai_YYYYMMDD_HHMMSS.log`

轮转策略（`log.c`）：

- 单文件上限：2MB
- 最大文件数量：5
- 超限后按文件名时间顺序删除最旧日志

注意：

- 如果系统时间不准，文件名时间戳会不准确，也可能影响 HTTPS/WSS 证书校验。

## 9. MCU 模拟器使用

用于本地联调主服务与串口协议，不依赖真实兔子板。

### 9.1 启动模拟器

```bash
cd /home/xm/proai/Proai_service/mcutools
python3 rabbit_mcu_sim.py
```

模拟器会创建：

- 符号链接：`/tmp/ttyModule`
- 主服务默认会连接该虚拟串口

### 9.2 再启动主服务

```bash
cd /home/xm/proai/Proai_service/main_service
./proai_service -s -v 0
```

可观察：

- 主服务下发心跳、查询、DP 控制
- 模拟器回包 `CMD 0x00/0x01/0x07` 等

## 10. Tongqu SDK 与工具链核心要点

### 10.1 当前仓库状态

当前仓库中可见：

- `Proai_service/tongqu-sdk.zip`（SDK 打包文件）
- `Proai_service/example/agent_sdk.h`（接口头）
- `Proai_service/doc/sdk调用文档.md`（详细文档）

项目构建脚本默认期望以下库目录（见 `CMakeLists.txt`）：

- `../tongqu-sdk/agent_linux_sdk_rockchip830`
- `../tongqu-sdk/rockchip830_runtime_bundle/lib`

如果目录不存在，需要先解压并按预期目录组织 SDK 资产。

### 10.2 运行时依赖

- `libagent_sdk`
- `libwebsockets`
- `libcurl`

目标板运行前通常需要设置：

```bash
export LD_LIBRARY_PATH=/root/workspace/proai/rockchip830_runtime_bundle/lib:$LD_LIBRARY_PATH
```

### 10.3 证书与 AI 连接

`cloud_llm.c` 中通过 `AGENT_CA_BUNDLE/SSL_CERT_FILE/CURL_CA_BUNDLE` 设置证书路径。
请确保路径在目标板真实存在，否则会出现 HTTPS/WSS 异常。

---

如需继续演进，建议下一步优先做两件事：

1. 在 `cloud_llm.c` 增加真实 JSON 指令解析，替换当前演示性入队逻辑。
2. 在 `uart_rx_thread` 增加标准化上报适配层，分别对接 AI/Tuya 的上行接口。

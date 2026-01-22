# Slave Service (从服务)

Slave Service 是运行在 AI 模组 (Linux) 上的硬件抽象层服务，负责管理与 MCU (座椅控制主板) 的串口通信，并通过 TCP Socket 暴露接口给主服务 (Master Service)。

## 1. 目录结构

```
slave_service/
├── main.c           # 主程序入口，包含 Socket Server 和 UART 轮询逻辑
├── tuya_protocol.c  # 涂鸦串口协议实现 (封包/解包)
├── tuya_protocol.h  # 涂鸦协议头文件
├── ipc_protocol.h   # 主从通信协议定义 (IPC)
└── Makefile         # 编译脚本
```

## 2. 编译指南

本工程使用标准 GCC 工具链。

### 2.1 交叉编译 (针对 ARM Linux 模组)
请修改 `Makefile` 中的 `CC` 变量为您 SDK 提供的交叉编译器，例如：
```makefile
CC = arm-linux-gnueabihf-gcc
```
然后运行：
```bash
make
```
生成的可执行文件为 `slave_service`。

### 2.2 本地测试编译 (x86/WSL)
直接运行：
```bash
make
```

## 3. 服务部署与启动

### 3.1 手动运行
将编译好的 `slave_service` 上传到模组 `/usr/bin/` 或 `/opt/proai/` 目录。
```bash
chmod +x slave_service
./slave_service
```
*注意*: 默认串口设备为 `/dev/ttyS1`，如需修改请编辑 `main.c` 中的 `UART_DEV` 宏。

### 3.2 开机自启动 (Systemd)

1. 创建服务文件 `/etc/systemd/system/slave_service.service`:
   ```ini
   [Unit]
   Description=ProAI Slave Service (UART Proxy)
   After=network.target

   [Service]
   Type=simple
   ExecStart=/opt/proai/slave_service
   Restart=always
   RestartSec=5
   User=root
   # 如果需要设置环境变量
   # Environment=LD_LIBRARY_PATH=/usr/lib

   [Install]
   WantedBy=multi-user.target
   ```

2. 启用并启动服务:
   ```bash
   systemctl daemon-reload
   systemctl enable slave_service
   systemctl start slave_service
   ```

## 4. 主从通信接口说明

Slave Service 监听 TCP 端口 `5555` (默认)。Master Service 作为 TCP Client 连接此端口。

### 4.1 通信协议 (IPC)
协议头定义 (大端序):
| 字段 | 长度 | 说明 |
| :--- | :--- | :--- |
| Magic | 2 Bytes | 固定 `0xA55A` |
| Type | 1 Byte | 消息类型 |
| Length | 2 Bytes | Payload 长度 |
| Payload | N Bytes | 数据载荷 |

### 4.2 消息类型

#### 1. 发送指令给 MCU (Master -> Slave)
- **Type**: `0x02` (IPC_CMD_SEND_TO_MCU)
- **Payload**: `[Cmd(1 Byte)] + [Data(N Bytes)]`
- **说明**: 
  - `Cmd` 为涂鸦串口协议命令字 (如 `0x06` 命令下发)。
  - `Data` 为涂鸦协议的数据单元。
  - Slave 收到后会自动封装帧头、校验和并发送给 MCU。

#### 2. 收到 MCU 数据 (Slave -> Master)
- **Type**: `0x03` (IPC_EVT_FROM_MCU)
- **Payload**: `[Cmd(1 Byte)] + [Data(N Bytes)]`
- **说明**:
  - Slave 收到 MCU 的完整帧后，剥离帧头和校验和，将核心数据透传给 Master。
  - Master 需根据 `Cmd` (如 `0x07` 状态上报) 解析后续数据。

#### 3. 心跳保活 (Master <-> Slave)
- **Type**: `0x01` (IPC_CMD_HEARTBEAT)
- **Payload**: 空
- **说明**: 用于维持 TCP 连接（可选，TCP本身有Keepalive）。

## 5. 常见问题排查

- **串口不通**: 
  - 检查 `main.c` 中 `UART_DEV` 是否正确。
  - 检查波特率是否匹配 (代码中默认为 9600)。
  - 使用 `dmesg | grep tty` 查看系统串口。
- **Socket 连接失败**:
  - 检查端口 5555 是否被占用 (`netstat -tuln | grep 5555`).
  - 检查防火墙设置。

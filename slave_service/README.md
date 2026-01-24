# Slave Service (从服务)

Slave Service 是运行在 AI 模组 (Linux) 上的硬件抽象层服务，负责管理与 MCU (座椅控制主板) 的串口通信，并通过 TCP Socket 暴露接口给主服务 (Master Service)。

lsof -i :5555
kill -9 114598

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

## 3. 服务部署与启动

### 3.1 开机自启动 (Systemd)

1. 创建服务文件 `/etc/systemd/system/slave_service.service`:

2. 启用并启动服务:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable slave_service
   sudo systemctl start slave_service
   sudo systemctl status slave_service

   sudo journalctl -u slave_service -f
   ```

## 4. 主从通信接口说明

Slave Service 监听 TCP 端口 `5555` (默认)。Master Service 作为 TCP Client 连接此端口。

### 4.1 通信协议 (IPC)

Master 与 Slave 之间使用 TCP 长连接进行通信，协议格式如下：

**数据包结构 (Big Endian):**
| 字段 | 长度 | 说明 |
| :--- | :--- | :--- |
| Length | 4 Bytes | 后续 JSON 数据的长度 (Network Byte Order) |
| JSON Body | N Bytes | 具体的消息内容，JSON 格式字符串 |

### 4.2 消息类型

所有消息的 JSON 根对象包含 `type` 和 `data` 两个字段。

#### 1. 发送指令给 MCU (Master -> Slave)
- **Type**: `"send_mcu"`
- **Data**:
  - `cmd`: (Integer) 涂鸦串口协议命令字 (如 `6` 代表命令下发)
  - `payload`: (String) 十六进制字符串，表示数据内容
- **示例**:
  ```json
  {
      "type": "send_mcu",
      "data": {
          "cmd": 6,
          "payload": "0101000101"
      }
  }
  ```
- **说明**: 
  - Slave 收到后会将 `payload` 解析为二进制，封装帧头、校验和后通过串口发送给 MCU。

#### 2. 收到 MCU 数据 (Slave -> Master)
- **Type**: `"evt_mcu"`
- **Data**:
  - `cmd`: (Integer) 涂鸦串口协议命令字
  - `payload`: (String) 十六进制字符串，表示数据内容
- **示例**:
  ```json
  {
      "type": "evt_mcu",
      "data": {
          "cmd": 7,
          "payload": "0101000100"
      }
  }
  ```
- **说明**:
  - 当 Slave 收到 MCU 的完整数据帧后，会解析出命令字和数据，以 JSON 格式推送给 Master。

#### 3. 其他消息
- 目前协议主要支持上述两种透传消息，后续可扩展心跳或状态查询消息。

## 5. 常见问题排查

- **串口不通**: 
  - 检查 `main.c` 中 `UART_DEV` 是否正确。
  - 检查波特率是否匹配 (代码中默认为 9600)。
  - 使用 `dmesg | grep tty` 查看系统串口。
- **Socket 连接失败**:
  - 检查端口 5555 是否被占用 (`netstat -tuln | grep 5555`).
  - 检查防火墙设置。

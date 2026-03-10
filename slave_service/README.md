# Slave Service (从服务)

Slave Service 是运行在 AI 模组 (Linux) 上的硬件抽象层服务，负责管理与 MCU (座椅控制主板) 的串口通信，并通过 TCP Socket 暴露接口给主服务 (Master Service)。

## 1. 功能特性
- **串口管理**: 独占打开 `/dev/ttySx`，处理波特率、校验位配置。
- **协议转换**: 实现 Tuya 串口协议的封包（加头、校验）与解包（状态机解析）。
- **OTA 托管**: 自动完成 MCU 固件升级流程，无需主服务干预细节。
- **IPC 服务**: 提供 TCP Server (Port 5555) 供主服务连接。

## 3. 服务部署与启动

1. 复制服务文件: `sudo cp slave_service.service /etc/systemd/system/`
2. 启用并启动:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable slave_service
   sudo systemctl start slave_service
   ```
3. 查看日志: `sudo journalctl -u slave_service -f`

## 4. 交互流程详解

### 4.1 场景一：控制设备 (Master -> MCU)
**Master** 发送指令打开设备，**Slave** 负责封包发送。
1. **Master**: 发送 `{"type": "send_mcu", "data": {"cmd": 6, "payload": "0101000101"}}`
2. **Slave**: 
   - 解析 JSON
   - 封装 Tuya 帧: `55 AA 00 06 00 05 01 01 00 01 01 CS`
   - 写入串口

### 4.2 场景二：状态上报 (MCU -> Master)
**MCU** 上报物理按键状态，**Slave** 解析后通知 Master。
1. **MCU**: 发送 `55 AA 03 07 00 05 ... CS`
2. **Slave**: 
   - 状态机校验帧头、版本、校验和
   - 提取 Cmd=`0x07`, Payload=`...`
   - 发送 `{"type": "evt_mcu", "data": {"cmd": 7, "payload": "..."}}` 到 Master

### 4.3 场景三：MCU OTA 升级
**Master** 仅需下发启动指令，**Slave** 自动完成后续所有交互。
1. **Master**: 发送 `{"type": "send_slave", "data": {"cmd": 10}}` (Cmd 10 = Start Upgrade)
2. **Slave**:
   - 读取本地固件 `/tmp/mcu_firmware.bin`
   - 与 MCU 握手 (0x0A)
   - 循环发送数据包 (0x0B)
   - 升级完成后通知 Master

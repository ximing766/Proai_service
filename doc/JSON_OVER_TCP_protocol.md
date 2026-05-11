# 语音服务接入协议

## 1. 连接参数

- 协议：TCP
- 地址：`127.0.0.1`
- 端口：`19090`
- 模式：长连接

## 2. 报文格式

- 编码：UTF-8 JSON
- 分帧：**每条消息必须以 `\n` 结尾**

## 3. 返回格式

成功：
no response

失败：

```json
{"ok":false,"message":"unsupported intent"}
```

## 4. 支持的 intent 列表

所有的 `intent` 均严格映射自云端定义的 `method` (数据点标识名)。对于需要下发状态的数据点，JSON 报文中需附带 `value` 字段。

### 4.1 通风控制 (DP105 set_fanSw)
```json
{"type":"intent","intent":"set_fanSw","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_fanSw","value":false,"source":"local_voice"}
```

### 4.2 加热控制 (DP101 set_hotSw)
```json
{"type":"intent","intent":"set_hotSw","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_hotSw","value":false,"source":"local_voice"}
```

### 4.3 自动模式 (DP120 set_autoMode)
```json
{"type":"intent","intent":"set_autoMode","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_autoMode","value":false,"source":"local_voice"}
```

### 4.4 氛围灯控制 (DP128 set_F_light)
```json
{"type":"intent","intent":"set_F_light","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_F_light","value":false,"source":"local_voice"}
```

### 4.5 音量与静音控制 (DP152 set_mute_mode_switch)
注：目前 DP 点仅支持静音开关控制，不支持无级音量调节。
```json
{"type":"intent","intent":"set_mute_mode_switch","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_mute_mode_switch","value":false,"source":"local_voice"}
```

### 4.6 侧保护开关 (DP117 set_protectionLeftSw, DP118 set_protectionRightSw)
```json
{"type":"intent","intent":"set_protectionLeftSw","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_protectionLeftSw","value":false,"source":"local_voice"}
{"type":"intent","intent":"set_protectionRightSw","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_protectionRightSw","value":false,"source":"local_voice"}
```

### 4.7 网络与设备绑定 (DP133 set_cloudUnbund)
```json
{"type":"intent","intent":"set_cloudUnbund","value":true,"source":"local_voice"}
```

### 4.8 状态查询 (DP110 get_batPercent, DP113 get_rssi)
```json
{"type":"intent","intent":"get_rssi","source":"local_voice"}
{"type":"intent","intent":"get_batPercent","source":"local_voice"}
```

### 4.9 旋转控制 (DP137 set_auto_rotate, DP138 set_assist_rotate, DP143 set_rotate_command)
```json
{"type":"intent","intent":"set_auto_rotate","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_auto_rotate","value":false,"source":"local_voice"}
{"type":"intent","intent":"set_assist_rotate","value":true,"source":"local_voice"}
{"type":"intent","intent":"set_assist_rotate","value":false,"source":"local_voice"}
{"type":"intent","intent":"set_rotate_command","value":1,"source":"local_voice"}
{"type":"intent","intent":"set_rotate_command","value":2,"source":"local_voice"}
{"type":"intent","intent":"set_rotate_command","value":3,"source":"local_voice"}
{"type":"intent","intent":"set_rotate_command","value":4,"source":"local_voice"}
```

### 4.10 座椅安装位置 (DP144 set_installation_position)
注：0 (false) 代表安装在主驾后，1 (true) 代表安装在副驾后。
```json
{"type":"intent","intent":"set_installation_position","value":false,"source":"local_voice"}
{"type":"intent","intent":"set_installation_position","value":true,"source":"local_voice"}
```


## 待定
- 通信采用 Length-Prefixed JSON 格式（大端序），以提高可扩展性：

| 字段 | 长度 | 说明 |
| :--- | :--- | :--- |
| **Length** | 4 Bytes | 后续 JSON 字符串的字节长度 (Big Endian) |
| **JSON** | N Bytes | UTF-8 编码的 JSON 字符串 |
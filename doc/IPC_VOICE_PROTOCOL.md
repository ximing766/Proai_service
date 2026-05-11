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

## 4. 支持的 intent

- `heater_on`
- `heater_off`
- 待定

## 5. 最小联调命令

```bash
printf '{"type":"ping"}\n' | nc 127.0.0.1 19090
```

```bash
printf '{"type":"intent","intent":"heater_on","source":"local_voice"}\n' | nc 127.0.0.1 19090
```

## 待定
通信采用 Length-Prefixed JSON 格式（大端序），以提高可扩展性：

| 字段 | 长度 | 说明 |
| :--- | :--- | :--- |
| **Length** | 4 Bytes | 后续 JSON 字符串的字节长度 (Big Endian) |
| **JSON** | N Bytes | UTF-8 编码的 JSON 字符串 |
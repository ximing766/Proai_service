# SDK 调用文档

作者：hh-zyb
最后更新：2026-05-04

## 1. 文档定位
本文档面向硬件端接入方，说明童趣设备侧 SDK 的接口功能、调用顺序和典型业务流程。

本文档重点覆盖：

- SDK 对外接口功能
- 设备配对与设备授权流程
- 文本链路、语音链路和 IOT 链路调用方式
- 音色参数、回调处理和错误处理
- 如何使用项目脚本验证 SDK 链路

## 2. SDK 职责边界
SDK 负责设备程序与云端语音服务之间的协议层能力。

SDK 当前负责：

- 建立和断开 WebSocket 连接
- 发送握手信息
- 发送文本消息
- 发送原始 JSON 消息
- 发送整段音频或流式音频分片
- 接收服务端文本事件
- 接收服务端 TTS 音频二进制数据
- 上报连接状态和错误
- 检查设备激活状态
- 申请设备配对码
- 使用 `device_id + device_secret` 获取设备 `access_token`
- 更新连接使用的 `Authorization`
- 在设备已绑定时自动获取 token 并建立授权连接

SDK 当前不负责：

- 本地麦克风采集
- 本地扬声器播放
- 音频编解码
- 设备屏幕、按键、灯效等业务 UI
- 用户在 App 或 Web 中输入配对码的流程
- 设备初始化向导内容填写
- token 后台定时刷新
- 音色克隆创建和删除

硬件端通常需要自己实现：

- 录音模块，把 PCM 数据交给 SDK
- 播放模块，播放 SDK 回调中的 TTS 音频
- 设备密钥安全存储
- 配对码显示或播报
- token 刷新策略
- IOT 指令落地执行逻辑

## 3. 核心概念
### 3.1 设备标识
- `device_id`：设备唯一 ID，由平台或厂商分配；生产环境建议显式写入，不要依赖默认回退值。
- `device_secret`：设备密钥，由平台生成，写入设备侧安全存储。
- `client_id`：本次客户端连接标识，建议同一设备内保持稳定或按进程生成；空字符串时 SDK 会自动生成默认值。

### 3.2 设备状态
- 未注册：平台没有该设备记录，SDK 无法完成配对和授权。
- 未绑定：设备已注册，但还没有被用户绑定。
- 已绑定：用户已通过配对码绑定设备。
- 已激活：设备已具备换取 `device_access_token` 并进入语音链路的条件。

### 3.3 连接地址
- `ws_url`：WebSocket 地址，例如 `ws://127.0.0.1/ws/v1/chat` 或 `wss://demo.example.com/ws/v1/chat`。
- `api_base_url`：HTTP API 基础地址，例如 `http://127.0.0.1` 或 `https://demo.example.com`。

如果没有显式设置 `api_base_url`，SDK 会尝试根据 `ws_url` 推导：

- `ws://host/path` 推导为 `http://host`
- `wss://host/path` 推导为 `https://host`

生产环境建议显式设置 `api_base_url`，避免反向代理路径或端口不一致导致设备接口请求失败。

### 3.4 音频格式
语音链路推荐输入：

- 格式：PCM/Opus
- 采样率：16000 Hz
- 声道：单声道
- 位深：16 bit little-endian
- 分片：20 ms 一片

20 ms 的 16kHz 单声道 16bit PCM 分片大小为 `640` 字节。

## 4. 数据结构
### 4.1 连接状态
```c
typedef enum AgentStatus {
    AGENT_STATUS_DISCONNECTED = 0,
    AGENT_STATUS_CONNECTING = 1,
    AGENT_STATUS_CONNECTED = 2,
    AGENT_STATUS_RECONNECTING = 3
} AgentStatus;
```

说明：

- `DISCONNECTED`：未连接或已断开。
- `CONNECTING`：正在建立连接。
- `CONNECTED`：连接已建立。
- `RECONNECTING`：SDK 正在自动重连。

注意：`RECONNECTING` 可通过 `agentGetConnectionStatus()` 主动查询，状态回调不一定主动上报该状态。

### 4.2 错误码
```c
typedef enum AgentErrorCode {
    AGENT_OK = 0,
    AGENT_ERR_INVALID_PARAM = -1,
    AGENT_ERR_NOT_CONNECTED = -2,
    AGENT_ERR_SEND_FAILED = -3,
    AGENT_ERR_TIMEOUT = -4,
    AGENT_ERR_INTERNAL = -5,
    AGENT_ERR_ALREADY_CONNECTED = -6,
    AGENT_ERR_STATE_INVALID = -7,
    AGENT_ERR_ALLOC_FAILED = -8,
    AGENT_ERR_JSON_PARSE = -9,
    AGENT_ERR_WS_INIT = -10,
    AGENT_ERR_HTTP_REQUEST = -11,
    AGENT_ERR_HTTP_RESPONSE = -12,
    AGENT_ERR_AUTH_MISSING = -13,
    AGENT_ERR_DEVICE_NOT_ACTIVATED = -14
} AgentErrorCode;
```

常见处理建议：

- `AGENT_ERR_INVALID_PARAM`：检查传入指针、字符串和音频长度。
- `AGENT_ERR_NOT_CONNECTED`：先连接，或等待重连完成后再发送。
- `AGENT_ERR_SEND_FAILED`：网络或 WebSocket 发送失败，可重连后重试当前轮。
- `AGENT_ERR_HTTP_REQUEST`：设备 HTTP 接口请求失败，检查网络、DNS、证书和 `api_base_url`。
- `AGENT_ERR_HTTP_RESPONSE`：服务端返回业务失败，检查设备 ID、密钥或接口权限。
- `AGENT_ERR_AUTH_MISSING`：缺少 `device_secret` 或 `Authorization`。
- `AGENT_ERR_DEVICE_NOT_ACTIVATED`：设备尚未绑定或尚未激活，应进入配对流程。

### 4.3 连接配置
```c
typedef struct AgentConfig {
    const char *ws_url;
    const char *device_id;
    const char *client_id;
    const char *authorization;
    const char *agent_id;
    const char *user_id;
    const char *tts_tone_id;
    const char *audio_format;
    int sample_rate;
    int channels;
    int frame_duration_ms;
    int feature_iot;
    int feature_speaker;
    int feature_mcp;
} AgentConfig;
```

字段说明：

| 字段 | 必填 | 建议值 | 说明 |
| --- | --- | --- | --- |
| `ws_url` | 是 | 部署方提供 | WebSocket 语音服务地址 |
| `device_id` | 建议显式传入 | 设备出厂写入 | 设备唯一 ID；空字符串会回退为 `device-default`，但生产设备不应依赖默认值 |
| `client_id` | 建议显式传入 | 设备内唯一 | 当前客户端标识；空字符串会自动生成默认值 |
| `authorization` | 否 | `Bearer <device_access_token>` | 已有 token 时可直接传入 |
| `agent_id` | 否 | 空字符串 | 正式设备建议留空，由服务端按绑定关系识别 |
| `user_id` | 否 | 空字符串 | 正式设备建议留空 |
| `tts_tone_id` | 否 | 空字符串 | 指定本轮使用的音色 ID |
| `audio_format` | 建议显式传入 | `pcm` | 当前推荐 PCM；空字符串会回退为 `pcm` |
| `sample_rate` | 建议显式传入 | `16000` | 采样率；小于等于 0 时回退为 `16000` |
| `channels` | 建议显式传入 | `1` | 声道数；小于等于 0 时回退为 `1` |
| `frame_duration_ms` | 建议显式传入 | `20` | 音频分片时长；小于等于 0 时回退为 `20` |
| `feature_iot` | 建议显式传入 | `1` | 是否启用 IOT 能力；`0` 关闭，非 `0` 开启 |
| `feature_speaker` | 建议显式传入 | `1` | 是否启用说话人能力；`0` 关闭，非 `0` 开启 |
| `feature_mcp` | 建议显式传入 | `0` | 是否启用 MCP 能力；`0` 关闭，非 `0` 开启 |

音色说明：

- 如果 `tts_tone_id` 为空，服务端会尝试按 `user_id + device_id + agent_id` 加载默认音色；任一条件缺失时，默认音色可能加载失败。
- 如果 `tts_tone_id` 不为空，服务端会优先使用该音色。
- 音色克隆、删除和默认音色管理由上层 App 或后台接口完成，硬件端通常只传入音色 ID 或留空使用默认音色。

### 4.4 设备激活状态
```c
typedef struct AgentDeviceActivationStatus {
    char m_device_id[65];
    char m_status[32];
    char m_setup_status[32];
    char m_current_agent_id[65];
    int m_activated_flag;
    int m_bound_flag;
    int m_can_request_pairing_code_flag;
} AgentDeviceActivationStatus;
```

字段说明：

- `m_device_id`：设备 ID。
- `m_status`：设备状态文本。
- `m_setup_status`：初始化状态文本。
- `m_current_agent_id`：当前设备绑定的智能体 ID。
- `m_activated_flag`：是否已激活。
- `m_bound_flag`：是否已绑定用户。
- `m_can_request_pairing_code_flag`：当前是否允许申请配对码。

### 4.5 配对码结果
```c
typedef struct AgentDevicePairingCodeResult {
    char m_device_id[65];
    char m_pairing_code[32];
    char m_pairing_expires_at[64];
    char m_status[32];
    char m_scene[32];
} AgentDevicePairingCodeResult;
```

字段说明：

- `m_pairing_code`：需要展示给用户的短时配对码。
- `m_pairing_expires_at`：配对码过期时间。
- `m_scene`：配对场景，当前设备初次绑定通常为 `INITIAL_CLAIM`。

### 4.6 设备 Token 结果
```c
typedef struct AgentDeviceTokenResult {
    char m_token_type[32];
    char m_access_token[2048];
    long m_expires_in;
    char m_device_id[65];
} AgentDeviceTokenResult;
```

字段说明：

- `m_token_type`：通常为 `Bearer`。
- `m_access_token`：设备访问 token。
- `m_expires_in`：有效期秒数。
- `m_device_id`：token 对应设备 ID。

拼接 WebSocket 授权头时使用：

```text
Bearer <access_token>
```

## 5. 回调接口
### 5.1 文本消息回调
```c
typedef void (*AgentMessageCallback)(const char *f_message, void *f_user_data);
```

用途：

- 接收服务端 JSON 文本事件。
- 处理握手确认、ASR 结果、LLM 文本、TTS 状态、IOT 指令等事件。

建议：

- 回调中只做快速解析和投递，不要长时间阻塞。
- 业务线程可通过队列消费这些事件。

### 5.2 音频回调
```c
typedef void (*AgentAudioCallback)(
        const unsigned char *f_audio_data,
        size_t f_audio_size,
        void *f_user_data
);
```

用途：

- 接收服务端返回的 TTS 音频二进制数据。
- 硬件端应将这些数据写入播放队列。

### 5.3 状态回调
```c
typedef void (*AgentStatusCallback)(AgentStatus f_status, void *f_user_data);
```

用途：

- 接收连接状态变化。
- 用于更新设备 UI、灯效或内部状态机。

### 5.4 错误回调
```c
typedef void (*AgentErrorCallback)(
        int f_error_code,
        const char *f_error_message,
        void *f_user_data
);
```

用途：

- 接收 SDK 内部错误。
- 用于日志、重试、重新配对或刷新授权。

## 6. SDK 返回格式总览

SDK 对设备应用层暴露的返回形态分为 6 类：

| 类型 | 到达方式 | 数量 | 说明 |
| --- | --- | ---: | --- |
| JSON 文本事件 | `AgentMessageCallback` | 常见 17 类 | 服务端 WebSocket 文本帧，业务层自行解析 JSON |
| TTS 音频二进制 | `AgentAudioCallback` | 1 类 | 服务端 WebSocket 二进制帧，通常直接送播放队列 |
| 连接状态枚举 | `AgentStatusCallback` / `agentGetConnectionStatus()` | 4 种 | 表示 SDK 连接状态 |
| 错误码和错误文本 | `AgentErrorCallback` / 同步函数返回值 | 15 个错误码值 | `0` 表示成功，负数表示错误 |
| 同步函数返回值 | 函数直接返回 | 4 种形态 | 指针、`int`、枚举或 `void` |
| 输出结构体 | 调用方传入结构体指针 | 3 种 | 设备激活状态、配对码、设备 token |

### 6.1 JSON 文本事件

`AgentMessageCallback` 收到的是服务端 WebSocket 文本帧，内容是完整 JSON 字符串。SDK 只负责透传，不负责解析业务语义。

这里不是“所有事件共用一个 JSON 模板”，而是“统一外壳 + 按事件族变化的字段集合”。

- 统一外层字段只有 `type`，少数事件还会带 `event`。
- 同一事件族的字段大体相近，但不同事件的字段并不相同。
- 调用方应先按 `type` 分流，再按子字段做兼容处理。

当前常见 JSON 事件按用途分为 6 组，共 17 类：

| 分组 | 事件 | 说明 |
| --- | --- | --- |
| 会话/心跳 | `session_ready` | WebSocket 会话已就绪，通常早于或接近 `hello` |
| 会话/心跳 | `hello` | 服务端握手响应，包含音频协商、模块选择等信息 |
| 会话/心跳 | `pong` | 心跳响应 |
| 会话/心跳 | `kickout` | 会话被服务端踢下线，例如授权缺失、重复连接或权限变化 |
| 音频流 | `audio_stream_started` | 服务端已进入收音状态 |
| 音频流 | `audio_stream_endpoint_detected` | 服务端检测到一句话结束 |
| 音频流 | `audio_stream_cancelled` | 当前流式音频输入已取消 |
| ASR/LLM | `asr_partial` | ASR 中间识别结果 |
| ASR/LLM | `asr_final` | ASR 最终文本，可能包含 `finalize_reason` 和 `fast_final` |
| ASR/LLM | `llm_partial` | LLM 流式增量文本 |
| ASR/LLM | `assistant_response` | 本轮最终回复摘要，包含意图、文本、工具调用摘要和诊断字段 |
| TTS 控制 | `tts_stream_start` | TTS 音频即将开始下行，真实音频走二进制回调 |
| TTS 控制 | `tts_stream_end` | TTS 音频下行结束，包含状态、字节数和分片数 |
| IOT | `iot` | 当前主要是 `event=descriptor_cached`；`command_dispatch` 体现在内部 trace / tool_calls，不是独立 WebSocket 事件 |
| 权限/限制 | `permission_update` | 权限状态更新 |
| 权限/限制 | `permission_limited` | 会话被权限、配额或策略限制 |
| 错误 | `error` | 服务端业务错误事件 |

#### 6.1.1 会话/心跳类

`session_ready` 代表 WebSocket 会话已经建立，字段如下：

```json
{
  "type": "session_ready",
  "message": "tongqu-server websocket connected",
  "service_version": "1.0.0",
  "session_id": "sess_xxx",
  "selected_module": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "supported_audio_formats": ["pcm", "opus"],
  "permission": {
    "enabled": true,
    "level": "standard",
    "tts_enabled": true
  }
}
```

- `selected_module` 是当前激活模块字典，固定键为 `vad/asr/llm/tts/intent/memory`。
- `supported_audio_formats` 是服务端支持的上下行音频格式列表。
- `permission` 是权限快照，完整字段为 `enabled`、`level`、`tts_enabled`、`max_conversations_per_minute`、`reason`、`tenant_id`、`quota`、`updated_at`、`authenticated`、`auth_subject_type`。

`hello` 是握手响应，除了会话信息，还会返回音频协商结果：

```json
{
  "type": "hello",
  "session_id": "sess_xxx",
  "audio": {
    "uplink": {
      "format": "pcm",
      "sample_rate": 16000,
      "channels": 1,
      "frame_duration_ms": 20
    },
    "downlink": {
      "format": "pcm",
      "sample_rate": 16000,
      "channels": 1,
      "frame_duration_ms": 20
    }
  },
  "feature_flags": {
    "speaker": true
  },
  "family_loaded": true,
  "tts_tone_id": "default",
  "tts_tone_loaded": true,
  "selected_module": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "permission": {
    "enabled": true,
    "level": "standard",
    "tts_enabled": true
  },
  "timestamp": "2026-05-04T08:00:00Z"
}
```

- `audio.uplink` / `audio.downlink` 都是协商结果对象，包含 `format`、`sample_rate`、`channels`、`frame_duration_ms`。
- `feature_flags` 是设备上报的特性开关。
- `family_loaded` 和 `tts_tone_loaded` 分别表示家庭上下文和音色配置是否已加载。

`pong` 只返回心跳时间戳：

```json
{
  "type": "pong",
  "timestamp": "2026-05-04T08:00:00Z"
}
```

`kickout` 有两种常见载荷：

```json
{
  "type": "kickout",
  "action": "disable",
  "reason": "authorization required",
  "stage": "connect"
}
```

```json
{
  "type": "kickout",
  "action": "disable",
  "target_type": "user",
  "target_id": "u_123",
  "reason": "permission changed",
  "permission": {
    "enabled": false,
    "level": "limited",
    "tts_enabled": false
  },
  "timestamp": "2026-05-04T08:00:00Z"
}
```

- 前一种通常用于鉴权缺失、会话未授权等场景。
- 后一种通常用于权限变更或目标对象被禁用时的主动踢出。
- 这两种载荷里的 `permission` 结构与 `session_ready`、`permission_update`、`permission_limited` 使用同一套权限快照。

#### 6.1.2 音频流类

`audio_stream_started` 代表开始收音：

```json
{
  "type": "audio_stream_started",
  "session_id": "sess_xxx",
  "session_state": "LISTENING",
  "audio_format": "pcm",
  "provider_trace": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "supports_partial": true,
  "timestamp": "2026-05-04T08:00:00Z"
}
```

`audio_stream_endpoint_detected` 有两种常见形态：

```json
{
  "type": "audio_stream_endpoint_detected",
  "reason": "vad_endpoint",
  "voice_chunk_count": 12,
  "silence_chunk_count": 4,
  "partial_text": "帮我开空调",
  "provider_trace": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "session_state": "LISTENING",
  "timestamp": "2026-05-04T08:00:00Z"
}
```

```json
{
  "type": "audio_stream_endpoint_detected",
  "reason": "asr_final",
  "voice_chunk_count": 12,
  "silence_chunk_count": 4,
  "partial_text": "帮我开空调",
  "provider_trace": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "provider_event": {
    "event": "asr_final",
    "request_id": "req_xxx"
  },
  "session_state": "LISTENING",
  "timestamp": "2026-05-04T08:00:00Z",
  "confirm_delay_ms": 180
}
```

- 第一种是 VAD 端点，通常只带基础统计字段。
- 第二种是上游 provider 确认后的端点，额外带 `provider_event` 和 `confirm_delay_ms`。

`audio_stream_cancelled` 表示当前输入流被取消：

```json
{
  "type": "audio_stream_cancelled",
  "session_state": "IDLE",
  "cancelled": true,
  "timestamp": "2026-05-04T08:00:00Z"
}
```

- `cancelled` 为 `true` 表示确实取消了当前流；当没有可取消流时，服务端也可能返回 `false`。

#### 6.1.3 ASR / LLM 类

`asr_partial` 是 ASR 中间结果：

```json
{
  "type": "asr_partial",
  "text": "帮我开",
  "is_final": false,
  "session_state": "LISTENING",
  "provider_trace": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  }
}
```

`asr_final` 是 ASR 最终结果：

```json
{
  "type": "asr_final",
  "text": "帮我开空调",
  "session_state": "LISTENING",
  "audio_input_bytes": 12800,
  "provider_trace": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "finalize_reason": "vad_endpoint",
  "fast_final": false
}
```

`llm_partial` 是 LLM 流式增量文本：

```json
{
  "type": "llm_partial",
  "delta_text": "好的，",
  "text": "好的，已经帮你处理。",
  "session_state": "THINKING",
  "provider_trace": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "stream_mode": true
}
```

`assistant_response` 是本轮最终回复摘要，字段会因场景模式、语音人脸识别和回复预算而增减：

```json
{
  "type": "assistant_response",
  "text": "好的，已经帮你处理。",
  "intent": "chat",
  "tool_calls": [],
  "speech_detected": true,
  "asr_text": "帮我开空调",
  "provider_trace": {
    "vad": "webrtc_vad",
    "asr": "funasr",
    "llm": "qwen",
    "tts": "edge_tts",
    "intent": "llm_intent",
    "memory": "redis_memory"
  },
  "tts_bytes_length": 2048,
  "audio_bytes_length": 2048,
  "audio_format": "pcm",
  "session_state": "IDLE",
  "memory_window_size": 20,
  "tts_tone_id": "default",
  "permission": {
    "enabled": true,
    "level": "standard",
    "tts_enabled": true
  }
}
```

常见 `assistant_response` 字段：

| 字段 | 说明 |
| --- | --- |
| `text` | 助手最终文本回复 |
| `echo` | 是否为回显模式回复 |
| `intent` | 本轮意图，例如 `chat / knowledge_query / iot_control / scene_enter / scene_exit` |
| `speech_detected` | 是否检测到语音输入 |
| `tool_calls` | LLM 工具调用或 IOT 命令摘要 |
| `asr_text` | 本轮 ASR 文本或文本输入 |
| `audio_bytes_length` / `tts_bytes_length` | 本轮下行音频长度统计 |
| `audio_format` | 下行音频格式 |
| `iot_request_id` / `iot_command_count` | IOT 请求编号和命令数量 |
| `tts_streaming` / `tts_stream_chunk_count` | 是否采用流式 TTS 以及分片数 |
| `memory_window_size` | 当前记忆窗口大小 |
| `tts_tone_id` | 本轮实际使用的音色 ID |
| `permission` | 当前权限快照 |
| `provider_trace` | ASR、LLM、TTS 等 provider 信息 |
| `knowledge_query_risk_level` / `knowledge_strict_mode` | 知识库查询风控信息 |
| `knowledge_hit_count` / `knowledge_titles` | 知识库命中信息 |
| `scene_active` | 当前是否处于情景模式 |
| `current_scene_code` / `current_scene_name` | 当前情景标识和名称 |
| `scene_event` | 情景切换结果，例如 `enter / exit / expired / resolve_failed` |
| `scene_phase_code` / `scene_phase_name` | 当前情景小闭环阶段 |
| `scene_round_count` | 当前情景内普通对话轮次 |
| `stream_mode` | 是否为流式输出场景 |
| `context_preview` | 上下文末尾预览 |
| `reply_original_length` / `reply_spoken_length` | 回复预算收口前后文本长度 |
| `reply_truncated` | 是否触发回复长度截断 |
| `reply_budget_intent` | 本轮采用的回复预算意图 |
| `tts_skipped_by_intent` | 是否因意图策略跳过云端 TTS |
| `speaker_id` / `speaker_name` / `speaker_similarity` | 识别到说话人时的附加字段 |

- `echo`、`knowledge_*`、`scene_*`、`reply_budget_*` 会按当前链路和模式动态增减。

注意：

- `tts_stream_start` 和 `tts_stream_end` 是 JSON 控制事件，不包含音频本体。
- TTS 音频本体通过 `AgentAudioCallback` 返回。
- IOT 控制类意图允许没有云端 TTS，硬件端可按固定播报词处理。
- `iot` 当前主要只有 `descriptor_cached` 这种外层事件，`command_dispatch` 更偏内部工具调用和 trace，不建议写成厂商侧独立 WebSocket 协议。
- `error` 事件通常至少带 `message`，部分异常还会附加 `transport` 和 `session_state`。

### 6.2 TTS 音频二进制返回

TTS 音频通过 `AgentAudioCallback` 返回：

```c
typedef void (*AgentAudioCallback)(
        const unsigned char *f_audio_data,
        size_t f_audio_size,
        void *f_user_data
);
```

说明：

- `f_audio_data` 是服务端下发的音频字节。
- `f_audio_size` 是本次回调的字节长度。
- 音频格式由握手阶段的 `audio_format` 协商结果决定，当前推荐 `pcm`。
- 业务层应尽快把音频写入播放队列，不要在回调内做阻塞播放。

### 6.3 连接状态枚举

连接状态一共 4 种：

| 枚举 | 值 | 含义 | 常见来源 |
| --- | ---: | --- | --- |
| `AGENT_STATUS_DISCONNECTED` | 0 | 未连接或已断开 | 初始状态、主动断开、连接失败后 |
| `AGENT_STATUS_CONNECTING` | 1 | 正在建立连接 | `agentConnect()` 或自动重连开始 |
| `AGENT_STATUS_CONNECTED` | 2 | 已建立 WebSocket 连接 | 连接成功后 |
| `AGENT_STATUS_RECONNECTING` | 3 | 正在自动重连 | 主要通过 `agentGetConnectionStatus()` 主动查询 |

注意：

- `AgentStatusCallback` 不一定主动上报 `RECONNECTING`，需要时调用 `agentGetConnectionStatus()` 查询。
- 发送文本、JSON 或音频前，建议确认状态为 `AGENT_STATUS_CONNECTED`。

### 6.4 错误码和错误文本

错误码定义在 `AgentErrorCode`，共 15 个枚举值：1 个成功码和 14 个错误码。

| 分类 | 错误码 | 值 | 说明 |
| --- | --- | ---: | --- |
| 成功 | `AGENT_OK` | 0 | 调用成功 |
| 参数/状态 | `AGENT_ERR_INVALID_PARAM` | -1 | 参数为空、长度非法或配置缺失 |
| 参数/状态 | `AGENT_ERR_NOT_CONNECTED` | -2 | 当前未连接，不能发送消息或音频 |
| 发送/网络 | `AGENT_ERR_SEND_FAILED` | -3 | WebSocket 文本、JSON、音频或控制消息发送失败 |
| 发送/网络 | `AGENT_ERR_TIMEOUT` | -4 | 连接、重连或等待过程超时 |
| 内部/资源 | `AGENT_ERR_INTERNAL` | -5 | SDK 内部编码、状态维护或断开连接失败 |
| 参数/状态 | `AGENT_ERR_ALREADY_CONNECTED` | -6 | 已连接时重复连接 |
| 参数/状态 | `AGENT_ERR_STATE_INVALID` | -7 | 当前状态不允许执行该操作 |
| 内部/资源 | `AGENT_ERR_ALLOC_FAILED` | -8 | 内存分配失败 |
| 内部/资源 | `AGENT_ERR_JSON_PARSE` | -9 | HTTP 响应或业务 JSON 解析失败 |
| 发送/网络 | `AGENT_ERR_WS_INIT` | -10 | WebSocket 初始化或建连失败 |
| HTTP/授权/设备 | `AGENT_ERR_HTTP_REQUEST` | -11 | HTTP 请求失败，例如 DNS、网络、TLS、curl 初始化失败 |
| HTTP/授权/设备 | `AGENT_ERR_HTTP_RESPONSE` | -12 | 服务端 HTTP 响应业务失败或响应结构异常 |
| HTTP/授权/设备 | `AGENT_ERR_AUTH_MISSING` | -13 | 缺少 `device_secret` 或 `Authorization` |
| HTTP/授权/设备 | `AGENT_ERR_DEVICE_NOT_ACTIVATED` | -14 | 设备未绑定或未激活，不能进入授权语音链路 |

错误文本通过 `AgentErrorCallback` 的 `f_error_message` 返回，和错误码配合使用。错误文本不是固定枚举，常见来源包括：

| 来源 | 常见错误文本示例 |
| --- | --- |
| WebSocket | `invalid_ws_url`、`lws_create_context_failed`、`lws_client_connect_failed`、`websocket_connect_timeout_or_failed`、`websocket_write_failed` |
| 握手/发送 | `encode_hello_failed`、`send_hello_failed`、`encode_text_failed`、`websocket_send_text_failed`、`websocket_send_json_failed`、`websocket_send_binary_failed` |
| 流式音频 | `encode_audio_stream_control_failed`、`websocket_send_audio_stream_control_failed` |
| 自动重连 | `auto_reconnect_exhausted` |
| 设备 HTTP 接口 | `http request parameter invalid`、`curl init failed`、`api response code missing`、`api response data missing`、`api response data invalid` |
| 授权/设备状态 | `device_secret missing`、`device not activated`、`build authorization failed`、`update authorization failed` |

处理建议：

- 业务判断优先使用错误码。
- 错误文本用于日志、排查和提示，不建议作为稳定协议做精确分支。
- HTTP 类错误需要结合设备 ID、设备密钥、绑定状态、`api_base_url` 和服务端响应排查。

### 6.5 同步函数返回值

SDK 同步接口有 4 种返回形态：

| 返回形态 | 代表接口 | 成功判断 | 失败判断 |
| --- | --- | --- | --- |
| 指针 | `agentCreateClient()` | 返回非空 `AgentClient *` | 返回 `NULL` |
| `int` | `agentConnect()`、`agentSendText()`、`agentSendAudioChunk()` 等 | 返回 `AGENT_OK` | 返回负数 `AgentErrorCode` |
| 枚举 | `agentGetConnectionStatus()` | 返回 `AgentStatus` 当前值 | 空客户端时返回 `AGENT_STATUS_DISCONNECTED` |
| `void` | `agentDestroyClient()`、`agentSetMessageCallback()` 等 | 无返回值 | 调用方保证参数和生命周期正确 |

返回 `int` 的接口包括：

| 接口 | 说明 |
| --- | --- |
| `agentConnect()` / `agentDisconnect()` | 建立或断开 WebSocket 连接 |
| `agentSetDeviceSecret()` / `agentSetApiBaseUrl()` / `agentUpdateAuthorization()` | 更新 SDK 本地配置 |
| `agentCheckDeviceActivation()` | 查询设备激活状态 |
| `agentFetchDevicePairingCode()` | 申请配对码 |
| `agentFetchDeviceToken()` | 使用设备密钥获取设备 token |
| `agentEnsureAuthorizedConnection()` | 检查激活状态、获取 token 并建立授权连接 |
| `agentSendText()` / `agentSendJson()` / `agentSendAudio()` | 发送文本、JSON 或整段音频 |
| `agentStartAudioStream()` / `agentSendAudioChunk()` / `agentFinishAudioStream()` / `agentCancelAudioStream()` | 流式音频控制和分片发送 |

### 6.6 输出结构体

SDK 当前有 3 种输出结构体，由调用方分配并传入指针，SDK 在成功时填充字段。

| 结构体 | 填充接口 | 用途 |
| --- | --- | --- |
| `AgentDeviceActivationStatus` | `agentCheckDeviceActivation()`、`agentEnsureAuthorizedConnection()` | 返回设备是否已绑定、已激活、是否可申请配对码 |
| `AgentDevicePairingCodeResult` | `agentFetchDevicePairingCode()` | 返回配对码、过期时间和配对场景 |
| `AgentDeviceTokenResult` | `agentFetchDeviceToken()`、`agentEnsureAuthorizedConnection()` | 返回 token 类型、access token、有效期和设备 ID |

`AgentDeviceActivationStatus` 字段：

| 字段 | 说明 |
| --- | --- |
| `m_device_id` | 设备 ID |
| `m_status` | 设备状态文本 |
| `m_setup_status` | 初始化状态文本 |
| `m_current_agent_id` | 当前设备绑定的智能体 ID |
| `m_activated_flag` | 是否已激活 |
| `m_bound_flag` | 是否已绑定用户 |
| `m_can_request_pairing_code_flag` | 当前是否允许申请配对码 |

`AgentDevicePairingCodeResult` 字段：

| 字段 | 说明 |
| --- | --- |
| `m_device_id` | 设备 ID |
| `m_pairing_code` | 需要展示给用户的短时配对码 |
| `m_pairing_expires_at` | 配对码过期时间 |
| `m_status` | 配对码状态 |
| `m_scene` | 配对场景，初次绑定通常为 `INITIAL_CLAIM` |

`AgentDeviceTokenResult` 字段：

| 字段 | 说明 |
| --- | --- |
| `m_token_type` | token 类型，通常为 `Bearer` |
| `m_access_token` | 设备访问 token |
| `m_expires_in` | 有效期秒数 |
| `m_device_id` | token 对应设备 ID |

注意：

- 输出结构体只在接口返回 `AGENT_OK` 时视为可信。
- 调用前建议将结构体清零，便于失败时避免误用旧数据。
- `agentEnsureAuthorizedConnection()` 的 `f_status` 和 `f_token` 可以传 `NULL`；不为空时会尽量返回本次检查和授权结果。

## 7. SDK 接口说明
### 7.1 创建与销毁
```c
AgentClient *agentCreateClient(const AgentConfig *f_config);
void agentDestroyClient(AgentClient *f_client);
```

使用方式：

1. 准备 `AgentConfig`。
2. 调用 `agentCreateClient()` 创建客户端。
3. 注册回调。
4. 完成业务后调用 `agentDestroyClient()` 释放客户端。

注意：

- `agentCreateClient()` 返回空指针表示参数或资源异常。
- 销毁前建议先调用 `agentDisconnect()`。

### 7.2 连接控制
```c
int agentConnect(AgentClient *f_client);
int agentDisconnect(AgentClient *f_client);
AgentStatus agentGetConnectionStatus(const AgentClient *f_client);
```

说明：

- `agentConnect()` 建立 WebSocket 连接，并发送握手消息。
- `agentDisconnect()` 主动断开连接。
- `agentGetConnectionStatus()` 查询当前连接状态。

典型判断：

```c
if (agentGetConnectionStatus(t_client) != AGENT_STATUS_CONNECTED) {
    agentConnect(t_client);
}
```

### 7.3 设备密钥与 API 地址
```c
int agentSetDeviceSecret(AgentClient *f_client, const char *f_device_secret);
int agentSetApiBaseUrl(AgentClient *f_client, const char *f_api_base_url);
```

说明：

- `agentSetDeviceSecret()` 设置设备密钥，用于设备状态检查、配对码申请和 token 获取。
- `agentSetApiBaseUrl()` 设置 HTTP API 基础地址。
- 如果不设置 `api_base_url`，SDK 会尝试从 `ws_url` 推导。

建议在设备启动后、调用任何设备 HTTP 辅助接口前完成设置。

### 7.4 授权更新
```c
int agentUpdateAuthorization(AgentClient *f_client, const char *f_authorization);
```

说明：

- 更新 SDK 内存中的 `Authorization`。
- 适用于设备 token 刷新后更新授权。
- 更新后如需立刻让新 token 生效，建议断开后重新连接。

### 7.5 设备状态检查
```c
int agentCheckDeviceActivation(
        AgentClient *f_client,
        AgentDeviceActivationStatus *f_status
);
```

说明：

- 使用 `device_id + device_secret` 查询设备是否已绑定、是否已激活。
- 未绑定设备应先申请配对码。
- 已绑定设备可继续获取设备 token。

### 7.6 申请配对码
```c
int agentFetchDevicePairingCode(
        AgentClient *f_client,
        AgentDevicePairingCodeResult *f_result
);
```

说明：

- 用于未绑定设备申请短时配对码。
- 设备拿到配对码后，应展示在屏幕、App 扫描页、串口日志或通过语音播报给用户。
- 用户需要在 Web 或 App 中输入配对码完成绑定。

### 7.7 获取设备 Token
```c
int agentFetchDeviceToken(
        AgentClient *f_client,
        AgentDeviceTokenResult *f_result
);
```

说明：

- 使用 `device_id + device_secret` 换取设备 `access_token`。
- 设备未绑定或未激活时会失败。
- 获取成功后可拼接 `Authorization` 并连接语音 WebSocket。

### 7.8 确保授权连接
```c
int agentEnsureAuthorizedConnection(
        AgentClient *f_client,
        AgentDeviceActivationStatus *f_status,
        AgentDeviceTokenResult *f_token
);
```

该接口会按顺序执行：

1. 检查设备激活状态。
2. 未激活或未绑定时返回 `AGENT_ERR_DEVICE_NOT_ACTIVATED`。
3. 获取设备 token。
4. 更新 `Authorization`。
5. 如果当前已连接，先断开。
6. 建立新的授权 WebSocket 连接。

适用场景：

- 设备已绑定，启动后想直接进入语音链路。
- 设备不知道本地 token 是否仍可用，希望由 SDK 重新获取。

### 7.9 文本发送
```c
int agentSendText(AgentClient *f_client, const char *f_text);
```

说明：

- 发送一条普通文本消息。
- SDK 会封装为服务端可识别的文本消息。
- 可用于简单问答、链路探活或无麦克风设备输入。

### 7.10 JSON 发送
```c
int agentSendJson(AgentClient *f_client, const char *f_json_text);
```

说明：

- 发送一段原始 JSON 文本。
- 适用于 IOT descriptor 上报等扩展协议。
- 调用方需要保证 JSON 格式正确。

### 7.11 整段音频发送
```c
int agentSendAudio(
        AgentClient *f_client,
        const unsigned char *f_audio_data,
        size_t f_audio_size
);
```

说明：

- 发送一段二进制音频数据，格式应与初始化时的 `audio_format` 保持一致。
- 当前实时语音推荐使用流式音频接口。

### 7.12 流式音频发送
```c
int agentStartAudioStream(AgentClient *f_client);
int agentSendAudioChunk(
        AgentClient *f_client,
        const unsigned char *f_audio_data,
        size_t f_audio_size
);
int agentFinishAudioStream(AgentClient *f_client);
int agentCancelAudioStream(AgentClient *f_client);
```

说明：

- `agentStartAudioStream()`：通知服务端开始接收一轮音频。
- `agentSendAudioChunk()`：发送一片 PCM 音频。
- `agentFinishAudioStream()`：通知服务端本轮音频结束。
- `agentCancelAudioStream()`：取消当前音频流。

推荐一轮语音调用顺序：

```c
agentStartAudioStream(t_client);

while (t_recording) {
    agentSendAudioChunk(t_client, t_pcm_frame, t_pcm_frame_size);
}

agentFinishAudioStream(t_client);
```

异常处理建议：

- 发送前确认连接状态为 `CONNECTED`。
- 任一发送接口返回非 0 时，停止当前轮，必要时调用 `agentCancelAudioStream()`。
- 如果用户取消本轮说话，调用 `agentCancelAudioStream()`，不要继续发送音频。

### 7.13 回调注册
```c
void agentSetMessageCallback(
        AgentClient *f_client,
        AgentMessageCallback f_message_callback,
        void *f_user_data
);

void agentSetStatusCallback(
        AgentClient *f_client,
        AgentStatusCallback f_status_callback,
        void *f_user_data
);

void agentSetAudioCallback(
        AgentClient *f_client,
        AgentAudioCallback f_audio_callback,
        void *f_user_data
);

void agentSetErrorCallback(
        AgentClient *f_client,
        AgentErrorCallback f_error_callback,
        void *f_user_data
);
```

说明：

- 创建客户端后、连接前注册回调。
- `f_user_data` 可传入业务上下文指针。
- 不需要某类回调时可以不注册，但语音链路建议至少注册消息、音频、状态和错误回调。

## 8. 推荐初始化流程
### 8.1 已有有效设备 Token
适用场景：

- 设备本地已经保存未过期的 `device_access_token`。
- 设备希望尽快建立 WebSocket 连接。

流程：

1. 准备 `AgentConfig`，填入 `authorization = "Bearer <device_access_token>"`。
2. 创建客户端。
3. 注册回调。
4. 调用 `agentConnect()`。
5. 收到 `hello` 或 `session_ready` 后进入业务链路。

示例：

```c
AgentConfig t_config = {
    .ws_url = "wss://demo.example.com/ws/v1/chat",
    .device_id = "device-001",
    .client_id = "device-001-main",
    .authorization = "Bearer <device_access_token>",
    .agent_id = "",
    .user_id = "",
    .tts_tone_id = "",
    .audio_format = "pcm",
    .sample_rate = 16000,
    .channels = 1,
    .frame_duration_ms = 20,
    .feature_iot = 1,
    .feature_speaker = 1,
    .feature_mcp = 0
};

AgentClient *t_client = agentCreateClient(&t_config);
agentSetMessageCallback(t_client, onMessage, t_context);
agentSetAudioCallback(t_client, onAudio, t_context);
agentSetStatusCallback(t_client, onStatus, t_context);
agentSetErrorCallback(t_client, onError, t_context);

int t_result = agentConnect(t_client);
```

### 8.2 仅有设备 ID 和设备密钥
适用场景：

- 设备本地只保存 `device_id + device_secret`。
- 设备启动时需要自动检查绑定状态并换取 token。

流程：

1. 创建客户端。
2. 设置 `device_secret`。
3. 设置 `api_base_url`。
4. 调用 `agentEnsureAuthorizedConnection()`。
5. 如果返回 `AGENT_OK`，说明已建立授权 WebSocket 连接。
6. 如果返回 `AGENT_ERR_DEVICE_NOT_ACTIVATED`，进入配对流程。

示例：

```c
AgentDeviceActivationStatus t_status;
AgentDeviceTokenResult t_token;

agentSetDeviceSecret(t_client, "<device_secret>");
agentSetApiBaseUrl(t_client, "https://demo.example.com");

int t_result = agentEnsureAuthorizedConnection(t_client, &t_status, &t_token);
if (t_result == AGENT_ERR_DEVICE_NOT_ACTIVATED) {
    /* 进入配对流程 */
}
```

## 9. 设备配对流程
### 9.1 业务流程
设备首次上电或恢复出厂后，推荐流程如下：

1. 设备读取本地 `device_id + device_secret`。
2. 调用 `agentCheckDeviceActivation()`。
3. 如果 `m_bound_flag = 1` 且 `m_activated_flag = 1`，进入设备授权流程。
4. 如果未绑定且 `m_can_request_pairing_code_flag = 1`，调用 `agentFetchDevicePairingCode()`。
5. 设备展示 `m_pairing_code`。
6. 用户在 App 或 Web 中输入配对码。
7. 设备定时调用 `agentCheckDeviceActivation()` 轮询绑定状态。
8. 状态变为已绑定后，调用 `agentEnsureAuthorizedConnection()` 进入语音链路。

### 9.2 设备侧伪代码
```c
AgentDeviceActivationStatus t_status;
AgentDevicePairingCodeResult t_pairing;

agentSetDeviceSecret(t_client, "<device_secret>");
agentSetApiBaseUrl(t_client, "https://demo.example.com");

int t_status_result = agentCheckDeviceActivation(t_client, &t_status);
if (t_status_result != AGENT_OK) {
    /* 记录错误并稍后重试 */
    return;
}

if (t_status.m_bound_flag && t_status.m_activated_flag) {
    agentEnsureAuthorizedConnection(t_client, NULL, NULL);
    return;
}

if (t_status.m_can_request_pairing_code_flag) {
    if (agentFetchDevicePairingCode(t_client, &t_pairing) == AGENT_OK) {
        showPairingCode(t_pairing.m_pairing_code, t_pairing.m_pairing_expires_at);
    }
}
```

### 9.3 配对码展示建议
硬件端可以根据产品形态选择：

- 屏幕显示配对码。
- App 局域网发现后展示配对码。
- 串口或日志输出配对码，仅用于调试。
- 语音播报配对码。
- 二维码承载配对码和设备 ID。

注意：

- 配对码是短时凭证，过期后需要重新申请。
- 已绑定设备不要重复申请配对码，避免用户误操作。
- 如果配对码申请失败，优先检查设备密钥和服务地址。

## 10. 设备授权流程
### 10.1 自动授权连接
设备已绑定时，最简单方式是调用：

```c
agentEnsureAuthorizedConnection(t_client, &t_status, &t_token);
```

成功后 SDK 已经完成：

- 获取设备 token
- 更新授权头
- 建立 WebSocket 连接

### 10.2 手动获取 Token
如果业务需要自己保存 token，可拆开调用：

```c
AgentDeviceTokenResult t_token;

if (agentFetchDeviceToken(t_client, &t_token) == AGENT_OK) {
    char t_auth[2200];
    snprintf(t_auth, sizeof(t_auth), "%s %s", t_token.m_token_type, t_token.m_access_token);
    agentUpdateAuthorization(t_client, t_auth);
    agentConnect(t_client);
}
```

### 10.3 Token 刷新
SDK 不会后台定时刷新 token。硬件端建议：

- 记录 `m_expires_in`。
- 在 token 过期前主动重新调用 `agentFetchDeviceToken()`。
- 调用 `agentUpdateAuthorization()` 更新授权。
- 必要时断开后重新连接。

如果服务端返回 `kickout` 且 reason 为 `authorization required`，通常说明缺少授权或授权失效，应重新获取 token 后连接。

## 11. 文本链路流程
文本链路适用于无麦克风调试、设备按键触发或屏幕文字输入。

调用流程：

1. 建立连接。
2. 调用 `agentSendText()`。
3. 通过 `AgentMessageCallback` 接收 `assistant_response`。
4. 如果服务端合成语音，也可能通过 `AgentAudioCallback` 收到音频。

示例：

```c
agentSendText(t_client, "你好，请介绍一下你自己");
```

常见消息：

- `hello`：握手确认。
- `assistant_response`：最终回复。
- `tts_stream_start`：TTS 开始。
- `tts_stream_end`：TTS 结束。

## 12. 语音链路流程
### 12.1 推荐实时语音流程
1. 建立授权连接。
2. 本地录音模块开始采集 PCM。
3. 调用 `agentStartAudioStream()`。
4. 每 20 ms 调用一次 `agentSendAudioChunk()`。
5. 用户停止说话、按键松开或本地 VAD 结束时调用 `agentFinishAudioStream()`。
6. 通过 `AgentMessageCallback` 处理 ASR、LLM、TTS 状态。
7. 通过 `AgentAudioCallback` 播放 TTS 音频。

### 12.2 上行音频要求
- 推荐 PCM 16kHz / 单声道 / 16bit little-endian。
- 推荐 20 ms 分片。
- 不要在一轮未结束时并发启动第二轮音频。
- 每轮开始必须先发送 `agentStartAudioStream()`。
- 每轮正常结束必须发送 `agentFinishAudioStream()`。
- 每轮取消必须发送 `agentCancelAudioStream()`。

### 12.3 常见服务端事件
语音回合中，消息回调可能收到：

| 事件 | 说明 |
| --- | --- |
| `audio_stream_started` | 服务端已进入收音状态 |
| `audio_stream_endpoint_detected` | 服务端检测到一句话结束 |
| `asr_partial` | ASR 中间结果 |
| `asr_final` | ASR 最终文本 |
| `llm_partial` | LLM 流式中间文本 |
| `tts_stream_start` | TTS 音频开始下行 |
| `tts_stream_end` | TTS 音频下行结束 |
| `assistant_response` | 本轮最终回复摘要 |
| `kickout` | 会话被服务端踢下线 |
| `permission_update` | 权限状态更新 |
| `permission_limited` | 会话被权限或配额限制 |
| `error` | 服务端业务错误 |

TTS 音频二进制通过 `AgentAudioCallback` 返回，不在 `AgentMessageCallback` 中返回。

会话建立阶段还可能先收到 `session_ready` 和 `hello`，它们不属于单轮语音回合，但通常是进入语音链路前必须处理的消息。

### 12.4 普通语音回合成功标志
普通问答型回合建议同时观察：

- 收到 `asr_final`
- 收到 `assistant_response`
- 如本轮有 TTS，收到 `tts_stream_end`
- `AgentAudioCallback` 收到音频数据
- 如本轮没有语音播报或被意图策略跳过，可能只收到 `assistant_response`，不会收到音频回调，也不会有 `tts_stream_*`

### 12.5 IOT 指令型语音回合成功标志
如果用户说的是控制设备类指令，例如“帮我打开演示灯”，LLM 可能返回 IOT 指令而不是可播报文本。

此时成功标志应看：

- `assistant_response.intent = iot_control`
- `assistant_response.iot_request_id` 有值
- `assistant_response.iot_command_count > 0`
- `assistant_response.tool_calls` 非空

IOT 指令型回合允许没有 `tts_stream_end`，也允许没有 TTS 音频；当前版本不会直接向设备侧下发独立的 `type = iot, event = command_dispatch` WebSocket 事件。

## 13. IOT 链路流程
### 13.1 开启 IOT 能力
创建 `AgentConfig` 时设置：

```c
.feature_iot = 1
```

连接成功后，服务端会知道当前会话支持 IOT。

### 13.2 上报 IOT 能力描述
设备需要通过 `agentSendJson()` 上报当前可被控制的能力。

示例：

```c
agentSendJson(
    t_client,
    "{\"type\":\"iot\",\"descriptors\":[{\"device\":\"demo_light\",\"method\":\"turn_on\",\"description\":\"打开演示灯\",\"parameters\":{\"power\":\"bool\"}}]}"
);
```

推荐 descriptor 字段：

| 字段 | 说明 |
| --- | --- |
| `device` | 设备或能力名，例如 `demo_light` |
| `method` | 可调用方法，例如 `turn_on` |
| `description` | 给 LLM 理解的能力描述 |
| `parameters` | 参数名和类型说明 |

服务端缓存成功后，消息回调会收到：

```json
{
  "type": "iot",
  "event": "descriptor_cached",
  "session_id": "sess_xxx",
  "descriptor_count": 1,
  "timestamp": "2026-05-04T08:00:00Z"
}
```

### 13.3 接收 IOT 指令
当前版本设备端不会直接收到 `command_dispatch` WebSocket 事件。用户语音触发控制意图时，设备端应优先观察 `assistant_response`：

```json
{
  "type": "assistant_response",
  "intent": "iot_control",
  "tool_calls": [
    {
      "device": "demo_light",
      "method": "turn_on",
      "parameters": {
        "power": true
      }
    }
  ],
  "iot_request_id": "req_xxx",
  "iot_command_count": 1
}
```

硬件端处理方式：

1. 判断 `assistant_response.intent = iot_control`。
2. 遍历 `tool_calls`。
3. 根据 `device + method` 路由到本地设备能力。
4. 校验 `parameters`。
5. 执行硬件动作。
6. 记录执行结果。

注意：

- 不要直接信任参数类型，执行前必须做范围和权限校验。
- 无法执行的指令应记录日志，并按业务需要反馈给上层。
- IOT descriptor 应在连接成功后尽早上报，避免用户先说控制指令时服务端还不知道设备能力。
- 服务端内部会把命令持久化为 `event = iot_command_dispatch` 的 trace 记录，但这不是厂商侧 WebSocket 协议。

## 14. 音色使用流程
硬件端不直接创建音色，通常只使用已有音色。

可选方式：

- `tts_tone_id` 留空：服务端按 `user_id + device_id + agent_id` 查询默认音色；三者缺一时可能无法加载默认音色。
- `tts_tone_id` 填具体音色 ID：当前会话优先使用该音色。

调用建议：

- 默认使用留空方式，减少硬件端状态管理。
- 如果 App 明确选择了某个音色，可把该音色 ID 下发给设备，设备连接时填入 `tts_tone_id`。
- 音色不可用时，服务端会回退到默认 TTS 配置，硬件端应以 `hello` 或 `assistant_response` 中的 `tts_tone_id` 信息做日志记录。

## 15. 自动重连与状态机建议
SDK 内部支持 WebSocket 心跳和非主动断线后的自动重连。

硬件端建议维护自己的业务状态：

- `BOOTING`：设备启动。
- `UNBOUND`：未绑定，等待配对。
- `PAIRING`：已展示配对码，等待用户绑定。
- `AUTHORIZED`：已获取 token。
- `VOICE_READY`：WebSocket 已连接，可语音交互。
- `VOICE_STREAMING`：正在发送一轮音频。
- `RECONNECTING`：网络恢复中。

处理建议：

- 只有 `CONNECTED` 状态下发送文本、JSON 或音频。
- 重连过程中暂停录音上行。
- 如果当前正在语音回合中断线，应丢弃本轮音频，重连后重新开始新一轮。
- token 过期或收到授权失败事件时，重新获取 token。

## 16. 完整设备启动流程
推荐硬件端启动流程：

1. 读取本地 `device_id + device_secret`。
2. 创建 SDK 客户端并注册回调。
3. 设置 `device_secret` 和 `api_base_url`。
4. 调用 `agentCheckDeviceActivation()`。
5. 如果未绑定，申请配对码并等待用户绑定。
6. 如果已绑定，调用 `agentEnsureAuthorizedConnection()`。
7. 连接成功后，如启用 IOT，调用 `agentSendJson()` 上报 descriptors。
8. 进入待唤醒或按键录音状态。
9. 用户触发语音输入后发送流式音频。
10. 播放 TTS 音频或执行 IOT 指令。
11. 定期刷新 token，处理断线重连。

## 17. 脚本验证 SDK
### 17.1 验证前提
脚本验证适合在联调阶段确认：

- 服务地址是否可达
- 设备 ID 和设备密钥是否正确
- 配对码流程是否可用
- 设备 token 是否可获取
- WebSocket 授权连接是否可建立
- 语音链路是否能完成 ASR、LLM、TTS
- IOT descriptor 和 command dispatch 是否可用

脚本验证不替代硬件业务程序，只用于缩短联调定位时间。

### 17.2 检查设备绑定状态
```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --check-activation-only
```

关注输出：

- `activated`
- `bound`
- `can_request_pairing_code`
- `current_agent_id`

### 17.3 仅申请配对码
```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --fetch-pairing-code-only
```

拿到配对码后，用户需要在 App 或 Web 中完成设备绑定。

### 17.4 自动设备流程验证
```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --auto-device-flow \
  --synthetic-input \
  --once
```

脚本会自动：

1. 检查设备绑定状态。
2. 未绑定时申请并打印配对码。
3. 等待用户完成绑定。
4. 获取设备 token。
5. 建立授权连接。
6. 用合成音频跑一轮语音链路。

### 17.5 使用 PCM 或 WAV 文件验证语音链路
PCM 文件：

```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --ensure-device-auth \
  --pcm-file /path/to/input.pcm \
  --once
```

WAV 文件：

```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --ensure-device-auth \
  --wav-file /path/to/input.wav \
  --once
```

文件要求：

- PCM：16kHz / 单声道 / 16bit little-endian。
- WAV：PCM 编码、16kHz、单声道、16bit。

### 17.6 使用本机麦克风验证真实语音链路
```bash
bash scripts/sdk_voice_remote_mic_wsl.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --username <username> \
  --password <password> \
  --device-id <device_id> \
  --agent-id <agent_id> \
  --client-id <client_id> \
  --record-seconds 5
```

说明：

- 该脚本使用用户名和密码登录，自动换取用户 `access_token`。
- 适合在电脑上做客户演示或真实语音链路联调。
- 硬件端正式接入仍建议使用设备 token 流程。

### 17.7 验证 IOT 语音控制
使用内置演示灯 descriptor：

```bash
bash scripts/sdk_voice_remote_mic_wsl.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --username <username> \
  --password <password> \
  --device-id <device_id> \
  --agent-id <agent_id> \
  --client-id <client_id> \
  --demo-iot-light \
  --record-seconds 6
```

录音时可说：

```text
帮我打开演示灯
```

成功标志：

- 日志出现 `descriptor_cached`
- 日志中记录到 `assistant_response.intent=iot_control`
- 日志中记录到 `assistant_response.iot_command_count>0`
- turn summary 中 `assistant_intent=iot_control`

### 17.8 验证音色参数
如果已有音色 ID，可在远端脚本中传入：

```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1/ws/v1/chat \
  --api-base-url http://127.0.0.1 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --ensure-device-auth \
  --tts-tone-id <tone_id> \
  --synthetic-input \
  --once
```

如果不传 `--tts-tone-id`，服务端会尝试使用该设备智能体的默认音色。

### 17.9 脚本日志成功标志
普通语音回合关注：

- `audio_stream_started`
- `asr_final`
- `assistant_response`
- `tts_stream_end`
- `turn summary`

IOT 指令回合关注：

- `descriptor_cached`
- `assistant_response.intent=iot_control`
- `assistant_response.iot_command_count`
- `assistant_intent=iot_control`

如果出现：

```text
kickout: authorization required
```

优先检查：

- 是否传入有效 `Authorization`
- 是否执行了 `--ensure-device-auth`
- 设备是否已绑定
- `device_secret` 是否正确
- `api_base_url` 是否正确

## 18. 硬件端接入检查清单
上线前建议逐项确认：

- 设备已安全保存 `device_id + device_secret`。
- 设备能访问 `api_base_url`。
- 设备能访问 `ws_url`。
- 未绑定设备能申请配对码。
- 用户能在 App 或 Web 中完成绑定。
- 已绑定设备能获取 `device_access_token`。
- WebSocket 能完成授权握手。
- 录音模块输出 PCM 16kHz / 单声道 / 16bit。
- 每轮语音都按 `start -> chunk -> finish` 顺序发送。
- TTS 音频回调能进入播放队列。
- IOT descriptor 在连接后完成上报，并能收到 `descriptor_cached`。
- 控制类回合能在 `assistant_response` 中看到 `intent=iot_control` 和 `tool_calls`。
- `tool_calls` 能路由到本地硬件能力。
- token 过期前有刷新策略。
- 断网重连时不会复用半截语音回合。
- 日志中能定位设备 ID、client ID、错误码和本轮 request ID。

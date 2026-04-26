# SDK调用文档

## 1. 文档目的

本文档面向 `agent_linux_sdk` 的接入方开发者，只说明当前 SDK 的实际调用方式、编译依赖和典型接入流程。

## 2. SDK 适用范围

`agent_linux_sdk` 是 Linux C SDK，适用于：

- 嵌入式 Linux 设备
- 运行在 Linux 容器中的设备侧程序
- 需要通过 WebSocket 接入童趣语音服务的 C/C++ 程序

当前 SDK 提供以下能力：

- WebSocket 建连、断连、自动重连、心跳保活
- 文本消息发送
- JSON 消息发送
- 整段音频发送
- 流式音频发送
- 文本消息、音频、状态、错误回调
- 设备侧激活状态检查
- 设备侧申请配对码
- 设备侧获取 `device_access_token`
- 更新 `Authorization` 并辅助建立已鉴权连接

当前 SDK 不负责：

- 本地录音和播放
- Opus 编解码
- 用户绑定设备
- 设备初始化向导
- 后台定时刷新 Token

## 3. 源码位置

- `agent_linux_sdk/include/agent_sdk.h`
- `agent_linux_sdk/src/agent.c`
- `agent_linux_sdk/src/device_api_client.c`
- `agent_linux_sdk/src/protocol.c`
- `agent_linux_sdk/src/websocket.c`
- `agent_linux_sdk/examples/simple_example.c`
- `agent_linux_sdk/examples/full_example.c`
- `agent_linux_sdk/examples/device_voice_example.c`
- `agent_linux_sdk/examples/reconnect_example.c`

## 4. 编译要求

### 4.1 系统依赖

构建 SDK 需要以下依赖：

- `cmake`
- `make`
- `pkg-config`
- `libwebsockets`
- `libcurl`
- `pthread`

如果使用 Debian / Ubuntu，可参考：

```bash
sudo apt-get update
sudo apt-get install -y cmake make pkg-config libwebsockets-dev libcurl4-openssl-dev
```

### 4.2 推荐构建方式

在 `WSL` 或 Linux 中执行：

```bash
cd /{workspace}
./scripts/build_agent_sdk_wsl.sh
```

构建完成后，产物位于：

```text
agent_linux_sdk/build_wsl_docker/
├─ libagent_sdk.so
├─ libagent_sdk.a
├─ libwebsockets.so.17
├─ simple_example
├─ full_example
├─ device_voice_example
└─ reconnect_example
```

说明：

- `libwebsockets.so.17` 是当前 WSL 主机使用 Docker 构建产物时额外带出的运行时依赖
- 它的用途是让本机 `sdk_voice_demo_remote.sh --sdk-profile native` 能直接加载 `libagent_sdk.so`
- 这不等于硬件量产包必须分发同名库，正式硬件接入仍应以目标设备自身 ABI 和运行库为准

### 4.3 直接使用 Docker 构建

```bash
docker build -t tongqu-agent-sdk agent_linux_sdk
container_id="$(docker create tongqu-agent-sdk)"
docker cp "${container_id}:/app/build/." agent_linux_sdk/build_docker/
docker rm -f "${container_id}"
```

### 4.4 使用自有交叉工具链

如果你们已有交叉工具链，可直接使用 `CMake`：

```bash
cmake -S agent_linux_sdk -B build.arm64 \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/your-toolchain.cmake
cmake --build build.arm64 -j
```

需要自行提供目标架构的：

- 编译器
- `libwebsockets`
- `libcurl`
- `pthread`
- `sysroot`

## 5. 对外头文件

头文件：

- `agent_linux_sdk/include/agent_sdk.h`

### 5.1 连接状态

```c
typedef enum AgentStatus {
    AGENT_STATUS_DISCONNECTED = 0,
    AGENT_STATUS_CONNECTING = 1,
    AGENT_STATUS_CONNECTED = 2,
    AGENT_STATUS_RECONNECTING = 3
} AgentStatus;
```

### 5.2 常用错误码

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

补充说明：

- `AGENT_ERR_HTTP_REQUEST = -11` 表示设备接口 HTTP/TLS 请求失败
- 当前 SDK 对瞬时网络错误已增加短时重试
- 如果公网 HTTPS 终端持续失败，仍可能返回 `-11`

### 5.3 连接配置结构体

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

- `ws_url`：语音服务 WebSocket 地址
- `device_id`：设备唯一标识
- `client_id`：客户端标识
- `authorization`：鉴权头，通常为 `Bearer <token>`
- `agent_id`：可选，通常由服务端根据设备自动绑定
- `user_id`：可选，设备正式接入时建议留空
- `tts_tone_id`：可选
- `audio_format`：推荐 `pcm`
- `sample_rate`：推荐 `16000`
- `channels`：推荐 `1`
- `frame_duration_ms`：推荐 `20`
- `feature_iot`：是否启用 IOT 能力
- `feature_speaker`：是否启用说话人能力
- `feature_mcp`：是否启用 MCP 能力

### 5.4 设备侧结果结构体

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

typedef struct AgentDevicePairingCodeResult {
    char m_device_id[65];
    char m_pairing_code[32];
    char m_pairing_expires_at[64];
    char m_status[32];
    char m_scene[32];
} AgentDevicePairingCodeResult;

typedef struct AgentDeviceTokenResult {
    char m_token_type[32];
    char m_access_token[2048];
    long m_expires_in;
    char m_device_id[65];
} AgentDeviceTokenResult;
```

## 6. 对外 API

### 6.1 创建与销毁

```c
AgentClient *agentCreateClient(const AgentConfig *f_config);
void agentDestroyClient(AgentClient *f_client);
```

### 6.2 连接控制

```c
int agentConnect(AgentClient *f_client);
int agentDisconnect(AgentClient *f_client);
AgentStatus agentGetConnectionStatus(const AgentClient *f_client);
```

说明：

- 自动重连期间可通过 `agentGetConnectionStatus()` 看到 `AGENT_STATUS_RECONNECTING`
- SDK 不会通过状态回调主动上报 `RECONNECTING`

### 6.3 设备侧辅助接口

```c
int agentSetDeviceSecret(AgentClient *f_client, const char *f_device_secret);
int agentSetApiBaseUrl(AgentClient *f_client, const char *f_api_base_url);
int agentUpdateAuthorization(AgentClient *f_client, const char *f_authorization);
int agentCheckDeviceActivation(AgentClient *f_client, AgentDeviceActivationStatus *f_status);
int agentFetchDevicePairingCode(AgentClient *f_client, AgentDevicePairingCodeResult *f_result);
int agentFetchDeviceToken(AgentClient *f_client, AgentDeviceTokenResult *f_result);
int agentEnsureAuthorizedConnection(
        AgentClient *f_client,
        AgentDeviceActivationStatus *f_status,
        AgentDeviceTokenResult *f_token
);
```

说明：

- `agentSetDeviceSecret()`：设置设备密钥
- `agentSetApiBaseUrl()`：设置 HTTP 接口基础地址；留空时根据 `ws_url` 自动推导
- `agentUpdateAuthorization()`：更新内存中的鉴权头
- `agentCheckDeviceActivation()`：检查设备是否已绑定、是否已激活
- `agentFetchDevicePairingCode()`：申请短时配对码
- `agentFetchDeviceToken()`：获取 `device_access_token`
- `agentEnsureAuthorizedConnection()`：自动检查设备状态、获取 token、更新授权并建立连接

### 6.4 消息发送

```c
int agentSendText(AgentClient *f_client, const char *f_text);
int agentSendJson(AgentClient *f_client, const char *f_json_text);
int agentSendAudio(AgentClient *f_client, const unsigned char *f_audio_data, size_t f_audio_size);
```

### 6.5 流式语音发送

```c
int agentStartAudioStream(AgentClient *f_client);
int agentSendAudioChunk(AgentClient *f_client, const unsigned char *f_audio_data, size_t f_audio_size);
int agentFinishAudioStream(AgentClient *f_client);
int agentCancelAudioStream(AgentClient *f_client);
```

### 6.6 回调注册

```c
void agentSetMessageCallback(AgentClient *f_client, AgentMessageCallback f_message_callback, void *f_user_data);
void agentSetStatusCallback(AgentClient *f_client, AgentStatusCallback f_status_callback, void *f_user_data);
void agentSetAudioCallback(AgentClient *f_client, AgentAudioCallback f_audio_callback, void *f_user_data);
void agentSetErrorCallback(AgentClient *f_client, AgentErrorCallback f_error_callback, void *f_user_data);
```

## 7. 推荐接入方式

### 7.1 已有 `device_access_token`

如果设备已经获取到有效 `device_access_token`，推荐直接这样连接：

```c
AgentConfig t_config = {
    .ws_url = "wss://demo.example.com/ws/v1/chat",
    .device_id = "device-001",
    .client_id = "client-001",
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
agentSetMessageCallback(t_client, onMessage, NULL);
agentSetAudioCallback(t_client, onAudio, NULL);
agentSetStatusCallback(t_client, onStatus, NULL);
agentSetErrorCallback(t_client, onError, NULL);

agentConnect(t_client);
```

说明：

- 设备正式接入时，建议 `agent_id / user_id` 留空
- 由服务端根据当前设备绑定关系自动确定用户和智能体

### 7.2 设备仅持有 `device_id + device_secret`

如果设备本地只保存：

- `device_id`
- `device_secret`

推荐调用顺序：

1. `agentSetDeviceSecret()`
2. `agentSetApiBaseUrl()`
3. `agentCheckDeviceActivation()`
4. `agentEnsureAuthorizedConnection()`

示例：

```c
AgentDeviceActivationStatus t_status;
AgentDeviceTokenResult t_token;

agentSetDeviceSecret(t_client, "<device_secret>");
agentSetApiBaseUrl(t_client, "https://demo.example.com");

if (agentEnsureAuthorizedConnection(t_client, &t_status, &t_token) != 0) {
    /* 处理错误 */
}
```

适用场景：

- 设备已绑定用户
- 设备启动后希望自动获取 token 并连接服务

### 7.3 未绑定设备申请配对码

如果设备尚未绑定用户，应该先申请配对码，不应直接连接语音链路。

示例：

```c
AgentDeviceActivationStatus t_status;
AgentDevicePairingCodeResult t_pairing_result;

agentSetDeviceSecret(t_client, "<device_secret>");
agentSetApiBaseUrl(t_client, "https://demo.example.com");

if (agentCheckDeviceActivation(t_client, &t_status) == 0) {
    if (!t_status.m_bound_flag && t_status.m_can_request_pairing_code_flag) {
        agentFetchDevicePairingCode(t_client, &t_pairing_result);
    }
}
```

设备拿到配对码后，应由用户在 Web/App 中输入配对码完成绑定。

## 8. 文本链路调用流程

```c
agentConnect(t_client);
agentSendText(t_client, "你好，童趣");
```

文本链路中，`AgentMessageCallback` 常见收到：

- `session_ready`
- `hello`
- `assistant_response`

## 9. 语音链路调用流程

### 9.1 上行建议

- 上行音频格式：`PCM 16kHz / 单声道 / 16bit`
- 分片时长：`20ms`
- 每轮流式语音开始前调用 `agentStartAudioStream()`
- 每轮结束后调用 `agentFinishAudioStream()`

示例：

```c
agentStartAudioStream(t_client);
while (有新的 PCM 帧) {
    agentSendAudioChunk(t_client, pcm_frame, pcm_size);
}
agentFinishAudioStream(t_client);
```

### 9.2 常见回包顺序

文本事件通过 `AgentMessageCallback` 返回：

1. `audio_stream_started`
2. `asr_final`
3. `llm_partial`
4. `tts_stream_start`
5. `tts_stream_end`
6. `assistant_response`

TTS 音频二进制通过 `AgentAudioCallback` 返回。

## 10. IOT 链路调用流程

上报 IOT 描述符示例：

```c
agentSendJson(
    t_client,
    "{\"type\":\"iot\",\"descriptors\":[{\"device\":\"demo_light\",\"method\":\"turn_on\",\"description\":\"Turn on the demo light\",\"parameters\":{\"power\":\"bool\"}}]}"
);
```

服务端可能返回：

- `descriptor_cached`
- `command_dispatch`

设备收到 `command_dispatch` 后按自身业务执行即可。

## 11. 自动重连与授权更新说明

当前 SDK 已内置：

- WebSocket 心跳保活
- 非主动断线后的自动重连
- 主动查询当前连接状态
- 手动更新 `authorization`
- 启动前按需获取设备 token

当前 SDK **没有**后台定时 Token 刷新线程。
如果业务需要更长时间运行，建议设备程序自行决定刷新时机，并在必要时调用：

```c
agentUpdateAuthorization(t_client, "Bearer <new_token>");
```

## 12. 远端语音链路验证脚本

推荐使用：

- `scripts/sdk_voice_demo_remote.sh`

该脚本支持：

- 仅检查设备激活状态
- 仅获取配对码
- 仅获取设备 token
- 自动检查当前设备是否已绑定
- 未绑定时自动申请并打印配对码
- 自动轮询等待用户在 Web 端完成绑定
- 自动确保设备授权连接
- 文件输入、PulseAudio 麦克风输入、合成音频输入
- 交互式麦克风多轮对话

### 12.0 Rockchip 830 交叉版运行包

如果验证设备是 `arm-rockchip830-linux-uclibcgnueabihf`，推荐先生成交叉版运行包：

```bash
bash Cross/compilation/package_sdk_voice_demo_rockchip830.sh \
  --rebuild \
  --libwebsockets-src /path/to/libwebsockets \
  --curl-src /path/to/curl \
  --download-mbedtls
```

输出位置：

- `Cross/compilation/output/rockchip830_runtime_bundle/`
- `Cross/compilation/output/rockchip830_runtime_bundle.tar.gz`

该运行包包含：

- `lib/libagent_sdk.so`
- `lib/libwebsockets.so*`
- `lib/libcurl.so*`
- `lib/libmbedtls.so* / lib/libmbedx509.so* / lib/libmbedcrypto.so*`
- `include/agent_sdk.h`
- `examples/device_voice_example`
- `scripts/sdk_voice_demo_remote.sh`
- `tools/sdk_voice_demo/sdk_voice_chat_demo.py`
- `certs/ca-certificates.crt`
- `run_public_voice_demo.sh`

注意：

- 这是**联调验证包**，主要用于快速验证设备网络、鉴权和语音链路
- 它不是正式量产接入包，不建议直接作为最终业务程序交付形态
- 该包只适用于 `arm-rockchip830-linux-uclibcgnueabihf`
- 目标设备仍需提供兼容的 `libc.so.0`
- `run_public_voice_demo.sh` 会默认优先加载交叉编译版 SDK
- `run_public_voice_demo.sh` 会自动导出 `AGENT_CA_BUNDLE / SSL_CERT_FILE / CURL_CA_BUNDLE / REQUESTS_CA_BUNDLE`
- 如果目标设备不是 `uClibc` ARM 环境，不能直接使用这个包
- `run_public_voice_demo.sh` 仍然面向 `PulseAudio` 设备
- 如果目标设备只有 `ALSA`，应直接运行 `examples/device_voice_example`

示例：

### 12.1 一体化现场演示

这是当前最推荐的演示方式。

适用场景：

- 设备尚未绑定
- 需要现场展示完整业务链
- 需要连续多轮语音对话

命令：

```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1:8765/ws/v1/chat \
  --api-base-url http://127.0.0.1:8080 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --source-name <Pulse输入源> \
  --sink-name <Pulse输出设备>
```

脚本会自动执行：

1. 检查当前设备是否已绑定
2. 如未绑定，自动申请并打印配对码
3. 等待用户在 Web 端输入配对码完成绑定
4. 绑定成功后自动获取 `device_access_token`
5. 自动建立授权连接
6. 调用麦克风录音
7. 上传语音
8. 接收下行语音并播放
9. 进入交互式多轮对话

### 12.2 仅检查设备状态

```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1:8765/ws/v1/chat \
  --api-base-url http://127.0.0.1:8080 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --check-activation-only
```

### 12.3 仅申请配对码

```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1:8765/ws/v1/chat \
  --api-base-url http://127.0.0.1:8080 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --fetch-pairing-code-only
```

### 12.4 自动获取 token 并连接语音链路

```bash
bash scripts/sdk_voice_demo_remote.sh \
  --ws-url ws://127.0.0.1:8765/ws/v1/chat \
  --api-base-url http://127.0.0.1:8080 \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --ensure-device-auth \
  --synthetic-input
```

### 12.5 多轮对话说明

默认行为：

- 交互式终端 + 麦克风模式：默认多轮
- 文件输入 / 合成音频输入：默认单轮

如果你希望强制单轮退出，可显式传：

```bash
--once
```

如果你希望设备未绑定时自动执行完整业务链，可显式传：

```bash
--auto-device-flow
```

可选超时参数：

```bash
--pairing-wait-timeout 300
--pairing-poll-interval 3
```

### 12.6 公网域名验证示例

```bash
bash run_public_voice_demo.sh \
  --ws-url wss://tongqu.zworker.online/ws/v1/chat \
  --api-base-url https://tongqu.zworker.online \
  --device-id <device_id> \
  --device-secret <device_secret> \
  --synthetic-input \
  --no-playback \
  --once
```

## 13. 接入建议

1. 设备程序长期保存：
   - `device_id`
   - `device_secret`
2. 不要把用户登录 token 写进设备固件
3. 已绑定设备优先走：
   - `agentEnsureAuthorizedConnection()`
4. 未绑定设备优先走：
   - `agentCheckDeviceActivation()`
   - `agentFetchDevicePairingCode()`
5. 现场演示推荐直接使用 `sdk_voice_demo_remote.sh` 的自动设备流程
6. 用户完成绑定后，设备再建立语音链路

## 13.1 硬件最小开发例程

当前推荐硬件接入方优先阅读并复用：

- `agent_linux_sdk/examples/device_voice_example.c`

这个例程覆盖了正式设备接入时最关键的调用顺序：

1. 读取 `AGENT_WS_URL / AGENT_API_BASE_URL / AGENT_DEVICE_ID / AGENT_DEVICE_SECRET`
2. 如需显式指定证书链，可额外设置 `AGENT_CA_BUNDLE`
3. `agentCreateClient()`
4. `agentSetDeviceSecret()`
5. `agentSetApiBaseUrl()`
6. `agentCheckDeviceActivation()`
7. 未绑定时调用 `agentFetchDevicePairingCode()` 并打印配对码
8. 已绑定时调用 `agentEnsureAuthorizedConnection()`
9. 使用 `agentStartAudioStream() / agentSendAudioChunk() / agentFinishAudioStream()` 发送 PCM 音频
10. 在文本/音频回调中读取 `session_ready / asr_final / tts_stream_end / assistant_response`

说明：

- 如果设置了 `AGENT_PCM_FILE`，例程会按 `16kHz / 单声道 / 16bit / 20ms` 分片发送 PCM 文件
- 如果设置了 `AGENT_ALSA_CAPTURE=1`，例程会调用 `arecord` 录制板载麦克风并上传
- 如果未设置 `AGENT_DISABLE_PLAYBACK`，例程会把返回 TTS PCM 落盘后调用 `aplay` 播放
- 可额外配置：
  - `AGENT_RECORD_SECONDS`
  - `AGENT_ALSA_CAPTURE_DEVICE`
  - `AGENT_ALSA_PLAYBACK_DEVICE`
  - `AGENT_TTS_SAMPLE_RATE`
- 如果设置了 `AGENT_CA_BUNDLE`，SDK 会显式把这份证书传给 `curl` 和 `libwebsockets`
- 如果未设置 `AGENT_PCM_FILE`，例程会发送内置测试帧，只用于演示流式发送调用顺序
- 硬件接入时，可直接把本地录音模块采集到的 PCM 数据替换到 `agentSendAudioChunk()` 的发送循环中

## 14. 硬件联调交付清单

建议按“正式接入包 + 快速验证包 + 参数清单”三部分交付。

### 14.1 正式接入包

这是硬件最终写设备程序时真正要依赖的内容：

- `agent_linux_sdk/include/agent_sdk.h`
- 目标架构对应的 `libagent_sdk.a` 或 `libagent_sdk.so`
- `agent_linux_sdk/examples/simple_example.c`
- `agent_linux_sdk/examples/full_example.c`
- `agent_linux_sdk/examples/device_voice_example.c`
- 本文档 `docs/sdk调用文档.md`

说明：

- 如果硬件程序静态链接，优先交付 `libagent_sdk.a`
- 如果硬件程序动态链接，除了 `libagent_sdk.so`，还要明确目标系统是否已具备 `libwebsockets`、`libcurl` 和兼容的 C 运行库
- SDK 头文件和库文件必须与硬件目标架构一致，不能把 WSL/x86 产物直接给 ARM 设备

### 14.2 快速验证包

这是给硬件先做“设备能不能连通服务”的联调包，不是最终量产接入形式。

如果目标设备兼容 `arm-rockchip830-linux-uclibcgnueabihf`，可直接交付：

- `Cross/compilation/output/rockchip830_runtime_bundle.tar.gz`

它包含：

- `run_public_voice_demo.sh`
- `scripts/sdk_voice_demo_remote.sh`
- `tools/sdk_voice_demo/sdk_voice_chat_demo.py`
- `libagent_sdk.so`
- `device_voice_example`
- `agent_sdk.h`
- `curl / libwebsockets / mbedTLS` 运行时依赖
- `CA` 证书文件

适用前提：

- 如果走 `run_public_voice_demo.sh`，设备上要有 `python3`
- 设备 CPU/ABI 与运行包匹配
- 如走 Python/PulseAudio 路线，设备上还要有 `parec / pacat / pactl`
- 如走 C 例程路线，设备上至少要有 `arecord / aplay`

### 14.3 必须同步给硬件的参数

- `ws_url`
  - 当前公网地址：`wss://tongqu.zworker.online/ws/v1/chat`
- `api_base_url`
  - 当前公网地址：`https://tongqu.zworker.online`
- `device_id`
- `device_secret`
- 音频参数约束
  - `PCM`
  - `16kHz`
  - `单声道`
  - `16bit`
  - `20ms` 分片

### 14.4 建议同步的联调说明

- 当前测试设备是否已经绑定用户
- 如果未绑定，是否要先走配对码流程
- 当前推荐连接方式：
  - 已绑定设备优先 `agentEnsureAuthorizedConnection()`
- 成功标志：
  - `session_ready`
  - `hello_audio`
  - `tts_stream_end`
  - `assistant_response`

### 14.5 安全建议

- `device_secret` 只应按设备逐台分发，不应写进仓库或长期出现在日志里
- 如果某台测试设备的密钥已经在聊天记录、共享终端或日志中暴露，建议在交给外部同事前先重置该设备密钥

## 15. 推荐联调顺序

建议硬件联调按下面顺序推进：

1. 先用快速验证包确认设备网络、TLS、鉴权和语音主链路通不通
2. 再用正式 SDK 包接入硬件自己的录音、播放和业务主程序
3. 最后再验证断网重连、重启后恢复、长连接和连续多轮对话

这样能先把“网络/凭据/服务端”问题和“硬件驱动/音频采集/业务代码”问题分开。

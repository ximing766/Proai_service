# 板端例程接入说明

## 1. 环境要求

- Linux 用户态环境
- `libagent_sdk.so`
- `arecord`
- `aplay`
- `sh` 或 `bash`
- 公网场景下可用的 CA 证书文件

如果走真实麦克风和扬声器，还需要：

- 麦克风设备可被 `arecord` 打开
- 扬声器设备可被 `aplay` 打开
- 录音设备没有被其他进程独占

## 2. 例程当前能力

`device_voice_example.c` 当前具备这些能力：

- 用 `device_id + device_secret` 做设备鉴权
- 建立 `ws / wss` 语音连接
- 支持公网 `https / wss`
- 支持局域网 `http / ws`
- 支持固定时长录音
- 支持边录边发
- 支持服务端 `VAD` 自动截断当前轮
- 支持下行 `TTS` 音频流式播放
- 保留本地 TTS PCM 临时文件，便于失败回溯

## 3. 源码编译方式

如果板端已经安装 SDK，并且 `libagent_sdk.so` 位于 `/usr/lib`，可以按下面方式编译：

```bash
gcc -O2 -o device_voice_example device_voice_example.c \
  -I. \
  -L/usr/lib \
  -lagent_sdk \
  -lpthread
```

如果 `agent_sdk.h` 不在当前目录，把 `-I.` 改成头文件实际目录。

如果链接器报缺少 `libwebsockets` 或 `libcurl`，再补：

```bash
gcc -O2 -o device_voice_example device_voice_example.c \
  -I./include \
  -L/usr/lib \
  -lagent_sdk \
  -lpthread \
  -lwebsockets \
  -lcurl
```

## 4. 运行时环境变量

### 4.1 必填

- `AGENT_WS_URL`
- `AGENT_DEVICE_ID`
- `AGENT_DEVICE_SECRET`

### 4.2 强烈建议填写

- `AGENT_API_BASE_URL`
- `AGENT_CA_BUNDLE`
- `SSL_CERT_FILE`
- `CURL_CA_BUNDLE`

### 4.3 板端音频相关

- `AGENT_ALSA_CAPTURE=1`
- `AGENT_RECORD_SECONDS`
- `AGENT_MAX_RECORD_SECONDS`
- `AGENT_ALSA_CAPTURE_DEVICE`
- `AGENT_ALSA_PLAYBACK_DEVICE`
- `AGENT_DISABLE_PLAYBACK`

说明：

- 设置 `AGENT_RECORD_SECONDS=5` 时，走固定 5 秒录音
- 不设置 `AGENT_RECORD_SECONDS`，或设置为 `0` 时，走“边录边发 + 服务端 VAD 自动截断”
- `AGENT_MAX_RECORD_SECONDS` 是 VAD 模式下的本地超时兜底

## 5. 运行示例

### 5.1 公网 `wss`，固定 5 秒录音

```bash
export LD_LIBRARY_PATH=/usr/lib:${LD_LIBRARY_PATH}
export AGENT_CA_BUNDLE=/root/ca-certificates.crt
export SSL_CERT_FILE=${AGENT_CA_BUNDLE}
export CURL_CA_BUNDLE=${AGENT_CA_BUNDLE}
export AGENT_WS_URL=wss://example.com/ws/v1/chat
export AGENT_API_BASE_URL=https://example.com
export AGENT_DEVICE_ID=<device_id>
export AGENT_DEVICE_SECRET=<device_secret>
export AGENT_ALSA_CAPTURE=1
export AGENT_RECORD_SECONDS=5
export AGENT_ALSA_CAPTURE_DEVICE=default
export AGENT_ALSA_PLAYBACK_DEVICE=default
./device_voice_example
```

### 5.2 公网 `wss`，边录边发 + VAD 自动截断

```bash
export LD_LIBRARY_PATH=/usr/lib:${LD_LIBRARY_PATH}
export AGENT_CA_BUNDLE=/root/ca-certificates.crt
export SSL_CERT_FILE=${AGENT_CA_BUNDLE}
export CURL_CA_BUNDLE=${AGENT_CA_BUNDLE}
export AGENT_WS_URL=wss://example.com/ws/v1/chat
export AGENT_API_BASE_URL=https://example.com
export AGENT_DEVICE_ID=<device_id>
export AGENT_DEVICE_SECRET=<device_secret>
export AGENT_ALSA_CAPTURE=1
unset AGENT_RECORD_SECONDS
export AGENT_MAX_RECORD_SECONDS=12
export AGENT_ALSA_CAPTURE_DEVICE=default
export AGENT_ALSA_PLAYBACK_DEVICE=default
./device_voice_example
```

### 5.3 局域网 `ws/http`

```bash
export LD_LIBRARY_PATH=/usr/lib:${LD_LIBRARY_PATH}
export AGENT_WS_URL=ws://192.168.1.90/ws/v1/chat
export AGENT_API_BASE_URL=http://192.168.1.90
export AGENT_DEVICE_ID=<device_id>
export AGENT_DEVICE_SECRET=<device_secret>
export AGENT_ALSA_CAPTURE=1
unset AGENT_RECORD_SECONDS
export AGENT_MAX_RECORD_SECONDS=12
export AGENT_ALSA_CAPTURE_DEVICE=default
export AGENT_ALSA_PLAYBACK_DEVICE=default
./device_voice_example
```

如果局域网语音服务直连 `8765`，把 `AGENT_WS_URL` 改成：

```bash
export AGENT_WS_URL=ws://192.168.1.90:8765/ws/v1/chat
```

## 6. 日志如何判断是否跑通

一轮完整成功时，通常会看到：

- `activation: ...`
- `authorized: token_type=Bearer ...`
- `session_ready`
- `audio_stream_started`
- `asr_final`
- `llm_partial`
- `tts_stream_start`
- `tts_stream_end`
- `summary: ...`

如果是 VAD 模式，还会看到：

- `recording_until_endpoint=true`
- `已开始录音，请讲话...`
- `audio_stream_endpoint_detected`

## 7. 常见问题

### 7.1 只打印 `missing env`

说明必填环境变量没给全，优先检查：

- `AGENT_WS_URL`
- `AGENT_DEVICE_ID`
- `AGENT_DEVICE_SECRET`

### 7.2 `设备未完成初始化` 或 `-14`

说明设备还没完成初始化，不是板端 C 例程问题。  
需要先在管理端把设备初始化完成。

### 7.3 `arecord: audio open error: Device or resource busy`

说明录音设备被其他进程占用。  
优先检查是否有本地唤醒词、录音守护或其他音频服务正在占用麦克风。

### 7.4 有 `tts_stream_start` 但没有声音

优先检查：

- `AGENT_ALSA_PLAYBACK_DEVICE` 是否正确
- `aplay` 是否能独立播放 PCM
- 扬声器链路是否正常

### 7.5 `asr_final` 为空

说明板端录到了音频，但云端没有识别出有效文本。  
优先检查：

- 麦克风输入电平
- 采集设备是否正确
- 环境噪声
- 云端当前实际生效的 ASR Provider

### 7.6 公网 `wss` 偶发超时

优先检查：

- 板端 DNS 解析
- 板端时间同步
- CA 证书路径
- IPv6 回退噪声


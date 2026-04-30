/**
 * Agent_linux_sdk 头文件
 * 作者: hh-zyb
 * 创建日期: 2026年03月05日
 * 描述: Agent Linux SDK 对外公开接口定义
 * 功能:
 *   - 定义 SDK 公共接口
 *   - 定义 AgentStatus 状态枚举
 *   - 定义 AgentErrorCode 错误码枚举
 *   - 定义 AgentConfig 配置结构体
 *   - 定义回调函数类型
 *   - 声明 AgentClient 不透明结构体
 */

#ifndef AGENT_SDK_H
#define AGENT_SDK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AgentStatus {
    AGENT_STATUS_DISCONNECTED = 0,
    AGENT_STATUS_CONNECTING = 1,
    AGENT_STATUS_CONNECTED = 2,
    AGENT_STATUS_RECONNECTING = 3
} AgentStatus;

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

#define AGENT_DEVICE_ID_MAX_LENGTH 64
#define AGENT_STATUS_TEXT_MAX_LENGTH 32
#define AGENT_PAIRING_CODE_MAX_LENGTH 32
#define AGENT_TIMESTAMP_TEXT_MAX_LENGTH 64
#define AGENT_TOKEN_TYPE_MAX_LENGTH 32
#define AGENT_ACCESS_TOKEN_MAX_LENGTH 2048

typedef struct AgentDeviceActivationStatus {
    char m_device_id[AGENT_DEVICE_ID_MAX_LENGTH + 1];
    char m_status[AGENT_STATUS_TEXT_MAX_LENGTH];
    char m_setup_status[AGENT_STATUS_TEXT_MAX_LENGTH];
    char m_current_agent_id[AGENT_DEVICE_ID_MAX_LENGTH + 1];
    int m_activated_flag;
    int m_bound_flag;
    int m_can_request_pairing_code_flag;
} AgentDeviceActivationStatus;

typedef struct AgentDevicePairingCodeResult {
    char m_device_id[AGENT_DEVICE_ID_MAX_LENGTH + 1];
    char m_pairing_code[AGENT_PAIRING_CODE_MAX_LENGTH];
    char m_pairing_expires_at[AGENT_TIMESTAMP_TEXT_MAX_LENGTH];
    char m_status[AGENT_STATUS_TEXT_MAX_LENGTH];
    char m_scene[AGENT_STATUS_TEXT_MAX_LENGTH];
} AgentDevicePairingCodeResult;

typedef struct AgentDeviceTokenResult {
    char m_token_type[AGENT_TOKEN_TYPE_MAX_LENGTH];
    char m_access_token[AGENT_ACCESS_TOKEN_MAX_LENGTH];
    long m_expires_in;
    char m_device_id[AGENT_DEVICE_ID_MAX_LENGTH + 1];
} AgentDeviceTokenResult;

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

typedef struct AgentClient AgentClient;

typedef void (*AgentMessageCallback)(const char *f_message, void *f_user_data);
typedef void (*AgentAudioCallback)(const unsigned char *f_audio_data, size_t f_audio_size, void *f_user_data);
typedef void (*AgentStatusCallback)(AgentStatus f_status, void *f_user_data);
typedef void (*AgentErrorCallback)(int f_error_code, const char *f_error_message, void *f_user_data);

/* 创建 SDK 客户端实例。 */
AgentClient *agentCreateClient(const AgentConfig *f_config);
/* 释放 SDK 客户端实例。 */
void agentDestroyClient(AgentClient *f_client);

/* 建立与服务端的 WebSocket 连接。 */
int agentConnect(AgentClient *f_client);
/* 主动断开与服务端的连接。 */
int agentDisconnect(AgentClient *f_client);
/* 获取当前连接状态。自动重连过程中的 RECONNECTING 状态仅支持主动查询，不通过状态回调主动上报。 */
AgentStatus agentGetConnectionStatus(const AgentClient *f_client);
/* 设置设备侧持久化密钥，用于检查激活状态、申请配对码和换取设备 token。 */
int agentSetDeviceSecret(AgentClient *f_client, const char *f_device_secret);
/* 设置 HTTP API 基础地址；为空时根据 ws_url 自动推导。 */
int agentSetApiBaseUrl(AgentClient *f_client, const char *f_api_base_url);
/* 更新 SDK 侧缓存的 authorization，供下一次握手或辅助重连使用。 */
int agentUpdateAuthorization(AgentClient *f_client, const char *f_authorization);
/* 检查当前设备是否已绑定/已激活。 */
int agentCheckDeviceActivation(AgentClient *f_client, AgentDeviceActivationStatus *f_status);
/* 申请当前设备短时配对码。 */
int agentFetchDevicePairingCode(AgentClient *f_client, AgentDevicePairingCodeResult *f_result);
/* 使用 device_id + device_secret 获取设备 access token。 */
int agentFetchDeviceToken(AgentClient *f_client, AgentDeviceTokenResult *f_result);
/* 确保已获取有效设备 token，并按需重连。 */
int agentEnsureAuthorizedConnection(
        AgentClient *f_client,
        AgentDeviceActivationStatus *f_status,
        AgentDeviceTokenResult *f_token
);
/* 发送文本消息。 */
int agentSendText(AgentClient *f_client, const char *f_text);
/* 发送 JSON 文本消息。 */
int agentSendJson(AgentClient *f_client, const char *f_json_text);
/* 发送整段音频数据。 */
int agentSendAudio(AgentClient *f_client, const unsigned char *f_audio_data, size_t f_audio_size);

/*
 * S1 流式音频接口。SDK 仅保持协议层职责，只负责发送控制消息和音频块，
 * 不内置采集和播放逻辑。
 */
int agentStartAudioStream(AgentClient *f_client);
/* 发送一段流式音频块。 */
int agentSendAudioChunk(AgentClient *f_client, const unsigned char *f_audio_data, size_t f_audio_size);
/* 通知服务端当前音频流结束。 */
int agentFinishAudioStream(AgentClient *f_client);
/* 取消当前音频流。 */
int agentCancelAudioStream(AgentClient *f_client);

/* 注册文本消息回调。 */
void agentSetMessageCallback(
        AgentClient *f_client,
        AgentMessageCallback f_message_callback,
        void *f_user_data
);

/* 注册连接状态回调。 */
void agentSetStatusCallback(
        AgentClient *f_client,
        AgentStatusCallback f_status_callback,
        void *f_user_data
);

/* 注册音频回调。 */
void agentSetAudioCallback(
        AgentClient *f_client,
        AgentAudioCallback f_audio_callback,
        void *f_user_data
);

/* 注册错误回调。 */
void agentSetErrorCallback(
        AgentClient *f_client,
        AgentErrorCallback f_error_callback,
        void *f_user_data
);

#ifdef __cplusplus
}
#endif

#endif

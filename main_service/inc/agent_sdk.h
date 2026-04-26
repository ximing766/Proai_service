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

typedef struct AgentClient AgentClient;

AgentClient *agentCreateClient(const AgentConfig *f_config);
void agentDestroyClient(AgentClient *f_client);

int agentConnect(AgentClient *f_client);
int agentDisconnect(AgentClient *f_client);
AgentStatus agentGetConnectionStatus(const AgentClient *f_client);

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

int agentSendText(AgentClient *f_client, const char *f_text);
int agentSendJson(AgentClient *f_client, const char *f_json_text);
int agentSendAudio(AgentClient *f_client, const unsigned char *f_audio_data, size_t f_audio_size);

int agentStartAudioStream(AgentClient *f_client);
int agentSendAudioChunk(AgentClient *f_client, const unsigned char *f_audio_data, size_t f_audio_size);
int agentFinishAudioStream(AgentClient *f_client);
int agentCancelAudioStream(AgentClient *f_client);

typedef void (*AgentMessageCallback)(AgentClient *client, const char *msg, void *user_data);
typedef void (*AgentStatusCallback)(AgentClient *client, AgentStatus status, void *user_data);
typedef void (*AgentAudioCallback)(AgentClient *client, const unsigned char *audio_data, size_t audio_size, void *user_data);
typedef void (*AgentErrorCallback)(AgentClient *client, AgentErrorCode error_code, const char *err_msg, void *user_data);

void agentSetMessageCallback(AgentClient *f_client, AgentMessageCallback f_message_callback, void *f_user_data);
void agentSetStatusCallback(AgentClient *f_client, AgentStatusCallback f_status_callback, void *f_user_data);
void agentSetAudioCallback(AgentClient *f_client, AgentAudioCallback f_audio_callback, void *f_user_data);
void agentSetErrorCallback(AgentClient *f_client, AgentErrorCallback f_error_callback, void *f_user_data);

#ifdef __cplusplus
}
#endif

#endif // AGENT_SDK_H
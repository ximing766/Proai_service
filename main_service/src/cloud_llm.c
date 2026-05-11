#include "../inc/cloud_llm.h"
#include "../inc/log.h"
#include "../inc/audio_module.h"
#include "../inc/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AgentClient *g_agent_client = NULL;
// static const char *k_ca_bundle_path = "/root/workspace/proai/ca-certificates.crt";
static const char *k_ca_bundle_path = "/root/ca-certificates.crt";

// IOT descriptors are extracted as standalone JSON strings for easy maintenance.
static const char *k_iot_descriptor_msg =
    "{\"type\":\"iot\",\"descriptors\":["
    "{\"device\":\"seat_controller\",\"method\":\"set_heater\",\"description\":\"Set seat heater on or off\",\"parameters\":{\"power\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_temperature\",\"description\":\"Set seat target temperature in Celsius\",\"parameters\":{\"temp\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_motor_level\",\"description\":\"Set seat motor intensity level\",\"parameters\":{\"level\":\"int\"}}"
    "]}";

static const char *json_get_string_or_default(const cJSON *obj, const char *key, const char *def_val) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return def_val;
}

static int json_get_int_or_default(const cJSON *obj, const char *key, int def_val) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return def_val;
}

static void log_tool_calls(const cJSON *root) {
    const cJSON *tool_calls = cJSON_GetObjectItemCaseSensitive(root, "tool_calls");
    if (!cJSON_IsArray(tool_calls)) return;

    int count = cJSON_GetArraySize(tool_calls);
    LOG_I("AI Assistant tool_calls count: %d", count);
    for (int i = 0; i < count; i++) {
        const cJSON *call = cJSON_GetArrayItem(tool_calls, i);
        char *raw = cJSON_PrintUnformatted(call);
        if (raw) {
            LOG_I("AI Assistant tool_call[%d]: %s", i, raw);
            free(raw);
        }
    }
}

static void cloud_llm_register_iot_capabilities(void) {
    if (!g_agent_client) return;

    int ret = agentSendJson(g_agent_client, k_iot_descriptor_msg);
    LOG_I("AI IOT Register: msg=%s, ret=%d", k_iot_descriptor_msg, ret);
}

static void on_message(const char *msg, void *user_data) {
    (void)user_data;
    if (msg == NULL) {
        LOG_W("AI Platform Message Recv: null");
        return;
    }
    LOG_I("AI Platform Message Recv: %s", msg);

    cJSON *root = cJSON_Parse(msg);
    if (root == NULL) {
        LOG_W("AI Platform Message Recv (non-json): %s", msg);
        return;
    }

    const char *type = json_get_string_or_default(root, "type", "");
    if (strcmp(type, "assistant_response") == 0) {
        const char *text = json_get_string_or_default(root, "text", "");
        const char *asr_text = json_get_string_or_default(root, "asr_text", "");
        int tts_bytes = json_get_int_or_default(root, "tts_bytes_length", 0);
        LOG_I("AI Assistant Text: %s", text);
        LOG_D("AI Assistant Meta: asr_text='%s', tts_bytes_length=%d", asr_text, tts_bytes);
        // log_tool_calls(root); // Only parse + log tool_calls, no execution here.
    } else if (strcmp(type, "error") == 0) {
        const char *message = json_get_string_or_default(root, "message", "unknown");
        LOG_E("AI Platform Error Message: %s", message);
    } else if (strcmp(type, "iot") == 0) {
        const char *event = json_get_string_or_default(root, "event", "");
        const char *session_id = json_get_string_or_default(root, "session_id", "");
        int descriptor_count = json_get_int_or_default(root, "descriptor_count", -1);
        LOG_I("AI IOT Event: event=%s, descriptor_count=%d, session_id=%s",
              event, descriptor_count, session_id);
    } else {
        LOG_I("AI Platform Message Recv (type=%s)", type[0] ? type : "unknown");
    }

    cJSON_Delete(root);
}

static void on_audio(const unsigned char *audio_data, size_t audio_size, void *user_data) {
    (void)user_data;
    // 直接交给本地音频模块处理（不走 IPC）
    (void)audio_module_play(audio_data, audio_size);
}

static void on_status(AgentStatus status, void *user_data) {
    const char *status_str = "UNKNOWN";
    switch (status) {
        case AGENT_STATUS_DISCONNECTED: status_str = "DISCONNECTED"; break;
        case AGENT_STATUS_CONNECTING:   status_str = "CONNECTING"; break;
        case AGENT_STATUS_CONNECTED:    status_str = "CONNECTED"; break;
        case AGENT_STATUS_RECONNECTING: status_str = "RECONNECTING"; break;
    }
    LOG_I("AI Platform Status Changed: %d (%s)", status, status_str);
}

static void on_error(int error_code, const char *err_msg, void *user_data) {
    LOG_E("AI Platform Error: %d - %s", error_code, err_msg ? err_msg : "Unknown");
}

int cloud_llm_init(const char *device_id, const char *device_secret) {
    if (g_agent_client) {
        LOG_W("AI Platform is already initialized.");
        return 0;
    }

    setenv("AGENT_CA_BUNDLE", k_ca_bundle_path, 1);
    setenv("SSL_CERT_FILE", k_ca_bundle_path, 1);
    setenv("CURL_CA_BUNDLE", k_ca_bundle_path, 1);

    AgentConfig config;
    memset(&config, 0, sizeof(AgentConfig));
    config.ws_url = "wss://tongqu.zworker.online/ws/v1/chat"; // 生产环境公网地址
    config.device_id = device_id;
    config.client_id = "proai-linux-client";
    config.authorization = ""; // 使用空字符串而非 NULL
    config.audio_format = "pcm";
    config.sample_rate = 16000;
    config.channels = 1;
    config.frame_duration_ms = 20;
    config.feature_iot = 1;
    config.feature_speaker = 0; // 强制文本模式，不请求 TTS 音频
    config.feature_mcp = 0;

    g_agent_client = agentCreateClient(&config);
    if (!g_agent_client) {
        LOG_E("Failed to create agent client");
        return -1;
    }

    agentSetMessageCallback(g_agent_client, on_message, NULL);
    agentSetAudioCallback(g_agent_client, on_audio, NULL);
    agentSetStatusCallback(g_agent_client, on_status, NULL);
    agentSetErrorCallback(g_agent_client, on_error, NULL);

    // 1. 设置设备密钥和 HTTP API 基础地址（SDK 将用于自动请求 Token）
    agentSetDeviceSecret(g_agent_client, device_secret);
    agentSetApiBaseUrl(g_agent_client, "https://tongqu.zworker.online");

    LOG_I("Checking device activation status and ensuring auth...");

    // 2. 检查激活状态并获取授权（这一步会阻塞直到成功或超时失败）
    // MYTODO 如果设备未绑定，这里会失败。正式产品中应该在这里检查未绑定状态并申请配对码（Pairing Code）给用户。
    AgentDeviceActivationStatus status;
    AgentDeviceTokenResult token_result;
    int ret = agentEnsureAuthorizedConnection(g_agent_client, &status, &token_result);
    
    if (ret != 0) {
        LOG_E("Failed to ensure authorized connection to AI platform, code: %d", ret);
        return -1;
    }

    LOG_I("AI Platform Connected Successfully! Token expires in: %ld", token_result.m_expires_in);
    cloud_llm_register_iot_capabilities();
    return 0;
}

int cloud_llm_send_text(const char *text) {
    if (!g_agent_client) {
        LOG_E("Cannot send text: AI Platform client not initialized.");
        return -1;
    }
    int ret = agentSendText(g_agent_client, text);
    LOG_I("AI Platform Sending Text: '%s' (Result Code: %d)", text, ret);
    return ret;
}

int cloud_llm_send_json(const char *json_str) {
    if (!g_agent_client) {
        LOG_E("Cannot send JSON: AI Platform client not initialized.");
        return -1;
    }
    int ret = agentSendJson(g_agent_client, json_str);
    LOG_I("AI Platform Sending JSON: %s (Result Code: %d)", json_str, ret);
    return ret;
}

int cloud_llm_send_audio(const uint8_t *data, size_t len) {
    if (!g_agent_client) {
        LOG_E("Cannot send audio chunk: AI Platform client not initialized.");
        return -1;
    }
    int ret = agentSendAudioChunk(g_agent_client, data, len);
    if (ret != 0) {
        LOG_E("Failed to send audio chunk to AI Platform, code: %d, len: %zu", ret, len);
    }
    return ret;
}

void cloud_llm_cleanup(void) {
    if (g_agent_client) {
        agentDisconnect(g_agent_client);
        agentDestroyClient(g_agent_client);
        g_agent_client = NULL;
        LOG_I("AI Platform Cleaned up.");
    }
}

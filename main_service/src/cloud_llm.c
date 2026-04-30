#include "../inc/cloud_llm.h"
#include "../inc/log.h"
#include "../inc/queue.h"
#include "../inc/audio_module.h"
#include "../inc/tuya_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AgentClient *g_agent_client = NULL;

static void on_message(const char *msg, void *user_data) {
    LOG_I("AI Platform Message Recv: %s", msg);
    
    // 解析下行指令并推入系统队列
    // 为了演示，这里假设所有从 AI 平台来的指令都打包成 CMD_DP_SEND
    // 实际项目中您需要解析 JSON (如使用 cJSON) 并提取真正的 DP 状态
    
    if (msg != NULL && strstr(msg, "assistant_response") != NULL) {
        SystemMsg s_msg;
        s_msg.type = MSG_TYPE_AI_CMD;
        s_msg.cmd = CMD_DP_SEND; // 假设这是一个控制指令
        s_msg.len = strlen(msg);
        s_msg.data = (uint8_t *)malloc(s_msg.len + 1);
        if (s_msg.data) {
            memcpy(s_msg.data, msg, s_msg.len);
            s_msg.data[s_msg.len] = '\0';
            msg_queue_push(&g_sys_queue, &s_msg);
        }
    }
}

static void on_audio(const unsigned char *audio_data, size_t audio_size, void *user_data) {
    // 旁路处理：音频数据直接丢给音频播放模块，不进主线程队列，防止阻塞控制流
    audio_module_play(audio_data, audio_size);
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

    setenv("AGENT_CA_BUNDLE", "/root/workspace/proai/cacert.pem", 1);
    setenv("SSL_CERT_FILE", "/root/workspace/proai/cacert.pem", 1);
    setenv("CURL_CA_BUNDLE", "/root/workspace/proai/cacert.pem", 1);

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
    config.feature_iot = 0; 
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
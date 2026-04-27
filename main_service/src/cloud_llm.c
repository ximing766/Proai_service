#include "../inc/cloud_llm.h"
#include "../inc/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AgentClient *g_agent_client = NULL;

static void on_message(const char *msg, void *user_data) {
    LOG_I("AI Platform Message Recv: %s", msg);
    // 仅仅打印Log，不执行实际操作
}

static void on_audio(const unsigned char *audio_data, size_t audio_size, void *user_data) {
    LOG_I("AI Platform Audio Recv: %zu bytes", audio_size);
    // 仅仅打印Log，不执行实际操作
}

static void on_status(AgentStatus status, void *user_data) {
    LOG_I("AI Platform Status Changed: %d", status);
}

static void on_error(int error_code, const char *err_msg, void *user_data) {
    LOG_E("AI Platform Error: %d - %s", error_code, err_msg ? err_msg : "Unknown");
}

int cloud_llm_init(const char *device_id, const char *client_id, const char *auth_token) {
    if (g_agent_client) {
        LOG_W("AI Platform is already initialized.");
        return 0;
    }

    AgentConfig config;
    memset(&config, 0, sizeof(AgentConfig));
    config.ws_url = "wss://tongqu.zworker.online/ws/v1/chat"; // 或者从外部传入
    config.device_id = device_id;
    config.client_id = client_id;
    config.authorization = auth_token;
    config.audio_format = "pcm";
    config.sample_rate = 16000;
    config.channels = 1;
    config.frame_duration_ms = 20;
    config.feature_iot = 1;
    config.feature_speaker = 1;
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

    // 直接连接，暂不处理复杂的鉴权和配对流程
    int ret = agentConnect(g_agent_client);
    if (ret != 0) {
        LOG_E("Failed to connect to AI platform, code: %d", ret);
        return -1;
    }

    LOG_I("AI Platform Initialized and Connecting...");
    return 0;
}

int cloud_llm_send_text(const char *text) {
    if (!g_agent_client) return -1;
    return agentSendText(g_agent_client, text);
}

int cloud_llm_send_json(const char *json_str) {
    if (!g_agent_client) return -1;
    return agentSendJson(g_agent_client, json_str);
}

int cloud_llm_send_audio(const uint8_t *data, size_t len) {
    if (!g_agent_client) return -1;
    return agentSendAudioChunk(g_agent_client, data, len);
}

void cloud_llm_cleanup(void) {
    if (g_agent_client) {
        agentDisconnect(g_agent_client);
        agentDestroyClient(g_agent_client);
        g_agent_client = NULL;
        LOG_I("AI Platform Cleaned up.");
    }
}
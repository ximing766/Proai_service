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

int cloud_llm_init(const char *device_id, const char *device_secret) {
    if (g_agent_client) {
        LOG_W("AI Platform is already initialized.");
        return 0;
    }

    AgentConfig config;
    memset(&config, 0, sizeof(AgentConfig));
    config.ws_url = "wss://tongqu.zworker.online/ws/v1/chat"; // 生产环境公网地址
    config.device_id = device_id;
    config.client_id = "proai-linux-client";
    // 初始时不直接传入 auth_token，而是通过设备激活流程动态获取
    config.authorization = NULL; 
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

    // 1. 设置设备密钥和 HTTP API 基础地址（SDK 将用于自动请求 Token）
    agentSetDeviceSecret(g_agent_client, device_secret);
    agentSetApiBaseUrl(g_agent_client, "https://tongqu.zworker.online");

    LOG_I("Checking device activation status and ensuring auth...");

    // 2. 检查激活状态并获取授权（这一步会阻塞直到成功或超时失败）
    // MYTODO 如果设备未绑定，这里会失败。正式产品中应该在这里检查未绑定状态并申请配对码（Pairing Code）给用户。
    // 为了目前的连通性测试，我们假设你使用的是已经绑定的测试 device_id。
    // 检查设备是否激活 -> 换取 Token -> 更新 Config 里的 authorization -> 建立 WebSocket 连接
    AgentDeviceActivationStatus status;
    AgentDeviceTokenResult token_result;
    int ret = agentEnsureAuthorizedConnection(g_agent_client, &status, &token_result);
    
    if (ret != 0) {
        LOG_E("Failed to ensure authorized connection to AI platform, code: %d", ret);
        // 这里如果是未激活/未绑定，可以调用 agentFetchDevicePairingCode 获取配对码打印出来
        return -1;
    }

    LOG_I("AI Platform Connected Successfully!");
    return 0;
}

int cloud_llm_send_text(const char *text) {
    if (!g_agent_client) {
        LOG_E("Cannot send text: AI Platform client not initialized.");
        return -1;
    }
    int ret = agentSendText(g_agent_client, text);
    if (ret != 0) {
        LOG_E("Failed to send text to AI Platform, code: %d", ret);
    }
    return ret;
}

int cloud_llm_send_json(const char *json_str) {
    if (!g_agent_client) {
        LOG_E("Cannot send JSON: AI Platform client not initialized.");
        return -1;
    }
    int ret = agentSendJson(g_agent_client, json_str);
    if (ret != 0) {
        LOG_E("Failed to send JSON to AI Platform, code: %d", ret);
    }
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
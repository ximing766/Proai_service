#include "../inc/cloud_llm.h"
#include "../inc/agent_sdk.h"
#include "../inc/log.h"
#include <stdio.h>
#include <stdlib.h>

static AgentClient *g_agent_client = NULL;

static void on_message(AgentClient *client, const char *msg, void *user_data) {
    LOG_I("Cloud LLM Message: %s", msg);
    // TODO: Parse msg (JSON) to extract IOT commands and forward to MCU
}

static void on_audio(AgentClient *client, const unsigned char *audio_data, size_t audio_size, void *user_data) {
    LOG_I("Cloud LLM Audio: %zu bytes", audio_size);
    // TODO: Play audio via ALSA or save to file
}

static void on_status(AgentClient *client, AgentStatus status, void *user_data) {
    LOG_I("Cloud LLM Status: %d", status);
}

static void on_error(AgentClient *client, AgentErrorCode error_code, const char *err_msg, void *user_data) {
    LOG_E("Cloud LLM Error: %d - %s", error_code, err_msg);
}

int cloud_llm_init(void) {
    AgentConfig config = {
        .ws_url = "wss://tongqu.zworker.online/ws/v1/chat",
        .device_id = "test-device-id",
        .client_id = "proai-linux",
        .authorization = "Bearer token", // Will be updated by auth flow
        .audio_format = "pcm",
        .sample_rate = 16000,
        .channels = 1,
        .frame_duration_ms = 20,
        .feature_iot = 1,
        .feature_speaker = 1,
        .feature_mcp = 0
    };

    g_agent_client = agentCreateClient(&config);
    if (!g_agent_client) {
        LOG_E("Failed to create agent client");
        return -1;
    }

    agentSetMessageCallback(g_agent_client, on_message, NULL);
    agentSetAudioCallback(g_agent_client, on_audio, NULL);
    agentSetStatusCallback(g_agent_client, on_status, NULL);
    agentSetErrorCallback(g_agent_client, on_error, NULL);

    // TODO: Implement proper device activation and token fetch
    // agentEnsureAuthorizedConnection(...)
    
    agentConnect(g_agent_client);
    LOG_I("Cloud LLM Initialized");
    return 0;
}

int cloud_llm_send_text(const char *text) {
    if (!g_agent_client) return -1;
    return agentSendText(g_agent_client, text);
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
    }
}
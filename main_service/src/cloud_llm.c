#include "../inc/cloud_llm.h"
#include "../inc/log.h"
#include "../inc/audio_module.h"
#include "../inc/cJSON.h"
#include "../inc/tuya_protocol.h"
#include "../inc/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AgentClient *g_agent_client = NULL;
// static const char *k_ca_bundle_path = "/root/workspace/proai/ca-certificates.crt";
static const char *k_ca_bundle_path = "/root/ca-certificates.crt";

// IOT descriptors are extracted as standalone JSON strings for easy maintenance.
static const char *k_iot_descriptor_msg =
    "{\"type\":\"iot\",\"descriptors\":["
    "{\"device\":\"seat_controller\",\"method\":\"set_hotSw\",\"description\":\"[DP101] 手动加热开关 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_autoHotTempTH\",\"description\":\"[DP102] 自动加热设定温度上限 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_autoFanTempTH\",\"description\":\"[DP103] 自动风扇设定温度上限 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_autoHotTempTL\",\"description\":\"[DP104] 自动加热设定温度下限 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_fanSw\",\"description\":\"[DP105] 手动通风开关 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_IMEI\",\"description\":\"[DP106] IMEI (只上报, type=raw)\",\"parameters\":{\"value\":\"string\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_mcuLpTimer\",\"description\":\"[DP107] 主动低功耗计时器 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_autoFanTempTL\",\"description\":\"[DP108] 自动风扇设定温度下限 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_recTemp\",\"description\":\"[DP109] 座椅温度 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_batPercent\",\"description\":\"[DP110] 电量百分比 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_batCharge\",\"description\":\"[DP111] 电池充电 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_leaveWarm\",\"description\":\"[DP112] 离车报警 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_rssi\",\"description\":\"[DP113] 信号强度 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_batWarm\",\"description\":\"[DP114] 电池报警 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_fanWarm\",\"description\":\"[DP115] 风扇异常报警 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_hotWarm\",\"description\":\"[DP116] 加热异常报警 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_protectionLeftSw\",\"description\":\"[DP117] 侧保护开关(左) (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_protectionRightSw\",\"description\":\"[DP118] 侧保护开关(右) (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_leftProtectionWarm\",\"description\":\"[DP119] 左侧保护报警 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_autoMode\",\"description\":\"[DP120] 自动模式 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_rightProtectionWarm\",\"description\":\"[DP121] 右侧保护报警 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_voiceModuleVersion\",\"description\":\"[DP122] 语音模组版本号 (只上报, type=raw)\",\"parameters\":{\"value\":\"string\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_ICCID\",\"description\":\"[DP123] ICCID (只上报, type=raw)\",\"parameters\":{\"value\":\"string\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_trafficSw\",\"description\":\"[DP124] 流量开关 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_sleepTimeSet\",\"description\":\"[DP125] 休眠时间设置 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_leaveWarmTimeSet\",\"description\":\"[DP126] 离车报警时间设置 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_noLoadModeRunTimeSet\",\"description\":\"[DP127] 非负载模式运行时间设置 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_F_light\",\"description\":\"[DP128] 氛围灯 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_trafficStartTime\",\"description\":\"[DP129] 流量起始时间 (只下发, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_trafficEndTime\",\"description\":\"[DP130] 流量结束时间 (只下发, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_GNSS\",\"description\":\"[DP131] 经纬度 (只上报, type=raw)\",\"parameters\":{\"value\":\"string\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_isRemoteMode\",\"description\":\"[DP132] 远程模式 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_cloudUnbund\",\"description\":\"[DP133] 云端解绑 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_hardWareVersion\",\"description\":\"[DP134] 硬件版本号 (只上报, type=raw)\",\"parameters\":{\"value\":\"string\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_seaton\",\"description\":\"[DP135] 落座状态 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_mcusleep\",\"description\":\"[DP136] mcu休眠 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_auto_rotate\",\"description\":\"[DP137] 自动旋转 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_assist_rotate\",\"description\":\"[DP138] 助力旋转 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_auto_rotate_ready\",\"description\":\"[DP139] 自动旋转校准状态 (只上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_auto_fan_temp\",\"description\":\"[DP140] 自动通风温度阈值 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_auto_heat_temp\",\"description\":\"[DP141] 自动加热温度阈值 (可下发可上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_rotary_position\",\"description\":\"[DP142] 座椅旋转位置值 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_rotate_command\",\"description\":\"[DP143] app旋转控制指令 (只下发, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_installation_position\",\"description\":\"[DP144] 座椅安装位置 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_longitude_value\",\"description\":\"[DP145] GPS经度值 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_longitude_ew\",\"description\":\"[DP146] 东西经度显示 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_latitude_value\",\"description\":\"[DP147] GPS纬度值 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_latitude_ns\",\"description\":\"[DP148] 南北纬度显示 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_lptime_onoff\",\"description\":\"[DP149] 定时唤醒开关 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_seat_tilt_position\",\"description\":\"[DP150] 座椅倾角位置 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"get_err_value\",\"description\":\"[DP151] 故障信息 (只上报, type=value)\",\"parameters\":{\"value\":\"int\"}},"
    "{\"device\":\"seat_controller\",\"method\":\"set_mute_mode_switch\",\"description\":\"[DP152] 静音模式开关 (可下发可上报, type=bool)\",\"parameters\":{\"value\":\"bool\"}}"
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

typedef struct {
    const char *method;
    uint8_t dp_id;
    uint8_t dp_type;
} iot_dp_map_t;

static const iot_dp_map_t g_iot_dp_map[] = {
    {"set_hotSw", 101, DP_TYPE_BOOL},
    {"set_autoHotTempTH", 102, DP_TYPE_VALUE},
    {"set_autoFanTempTH", 103, DP_TYPE_VALUE},
    {"set_autoHotTempTL", 104, DP_TYPE_VALUE},
    {"set_fanSw", 105, DP_TYPE_BOOL},
    {"get_IMEI", 106, DP_TYPE_RAW},
    {"get_mcuLpTimer", 107, DP_TYPE_VALUE},
    {"set_autoFanTempTL", 108, DP_TYPE_VALUE},
    {"get_recTemp", 109, DP_TYPE_VALUE},
    {"get_batPercent", 110, DP_TYPE_VALUE},
    {"get_batCharge", 111, DP_TYPE_BOOL},
    {"get_leaveWarm", 112, DP_TYPE_BOOL},
    {"get_rssi", 113, DP_TYPE_VALUE},
    {"get_batWarm", 114, DP_TYPE_BOOL},
    {"get_fanWarm", 115, DP_TYPE_BOOL},
    {"get_hotWarm", 116, DP_TYPE_BOOL},
    {"set_protectionLeftSw", 117, DP_TYPE_BOOL},
    {"set_protectionRightSw", 118, DP_TYPE_BOOL},
    {"get_leftProtectionWarm", 119, DP_TYPE_BOOL},
    {"set_autoMode", 120, DP_TYPE_BOOL},
    {"get_rightProtectionWarm", 121, DP_TYPE_BOOL},
    {"get_voiceModuleVersion", 122, DP_TYPE_RAW},
    {"get_ICCID", 123, DP_TYPE_RAW},
    {"set_trafficSw", 124, DP_TYPE_BOOL},
    {"set_sleepTimeSet", 125, DP_TYPE_VALUE},
    {"set_leaveWarmTimeSet", 126, DP_TYPE_VALUE},
    {"set_noLoadModeRunTimeSet", 127, DP_TYPE_VALUE},
    {"set_F_light", 128, DP_TYPE_BOOL},
    {"set_trafficStartTime", 129, DP_TYPE_VALUE},
    {"set_trafficEndTime", 130, DP_TYPE_VALUE},
    {"get_GNSS", 131, DP_TYPE_RAW},
    {"get_isRemoteMode", 132, DP_TYPE_BOOL},
    {"set_cloudUnbund", 133, DP_TYPE_BOOL},
    {"get_hardWareVersion", 134, DP_TYPE_RAW},
    {"get_seaton", 135, DP_TYPE_BOOL},
    {"get_mcusleep", 136, DP_TYPE_BOOL},
    {"set_auto_rotate", 137, DP_TYPE_BOOL},
    {"set_assist_rotate", 138, DP_TYPE_BOOL},
    {"get_auto_rotate_ready", 139, DP_TYPE_BOOL},
    {"set_auto_fan_temp", 140, DP_TYPE_VALUE},
    {"set_auto_heat_temp", 141, DP_TYPE_VALUE},
    {"get_rotary_position", 142, DP_TYPE_VALUE},
    {"set_rotate_command", 143, DP_TYPE_VALUE},
    {"set_installation_position", 144, DP_TYPE_BOOL},
    {"get_longitude_value", 145, DP_TYPE_VALUE},
    {"get_longitude_ew", 146, DP_TYPE_VALUE},
    {"get_latitude_value", 147, DP_TYPE_VALUE},
    {"get_latitude_ns", 148, DP_TYPE_VALUE},
    {"set_lptime_onoff", 149, DP_TYPE_BOOL},
    {"get_seat_tilt_position", 150, DP_TYPE_VALUE},
    {"get_err_value", 151, DP_TYPE_VALUE},
    {"set_mute_mode_switch", 152, DP_TYPE_BOOL},
};

int execute_single_iot_call(const char *method, const cJSON *val_item, int is_offline_voice) {
    if (!method || !val_item) {
        LOG_E("Invalid method or value item for AI tool_call");
        return -1;
    }

    // Find DP map
    const iot_dp_map_t *dp_map = NULL;
    for (size_t j = 0; j < sizeof(g_iot_dp_map)/sizeof(g_iot_dp_map[0]); j++) {
        if (strcmp(method, g_iot_dp_map[j].method) == 0) {
            dp_map = &g_iot_dp_map[j];
            break;
        }
    }
    
    if (!dp_map) {
        LOG_W("AI tool_call method '%s' not mapped to DP", method);
        return -1;
    }

    uint8_t *payload = malloc(128); // Safe enough for DP payload
    if (!payload) return -1;
    
    int payload_len = -1;
    if (dp_map->dp_type == DP_TYPE_BOOL) {
        uint8_t v = cJSON_IsTrue(val_item) ? 1 : 0;
        payload_len = tuya_pack_dp_bool(dp_map->dp_id, v, payload, 64);
    } else if (dp_map->dp_type == DP_TYPE_VALUE) {
        int32_t v = val_item->valueint;
        payload_len = tuya_pack_dp_value(dp_map->dp_id, v, payload, 64);
    } else if (dp_map->dp_type == DP_TYPE_ENUM) {
        uint8_t v = val_item->valueint;
        payload_len = tuya_pack_dp_enum(dp_map->dp_id, v, payload, 64);
    } else {
        LOG_W("Unsupported DP Type %d for method %s", dp_map->dp_type, method);
        free(payload);
        return -1;
    }

    if (payload_len <= 0) {
        free(payload);
        return -1;
    }

    SystemMsg msg = {.type = is_offline_voice ? MSG_TYPE_OFFLINE_VOICE_CMD : MSG_TYPE_AI_CMD,
                     .cmd = CMD_DP_SEND, .data = payload, .len = payload_len};

    if (msg_queue_push(&g_sys_queue, &msg) != 0) {
        free(payload);
        LOG_E("Failed to enqueue AI tool_call to main thread");
        return -1;
    }

    LOG_I("Enqueued DP control: method=%s, dp_id=%d (src: %s)",
          method, dp_map->dp_id, is_offline_voice ? "IPC" : "LLM");
    return 0;
}

static void execute_iot_tool_calls(const cJSON *root) {
    const cJSON *tool_calls = cJSON_GetObjectItemCaseSensitive(root, "tool_calls");
    if (!cJSON_IsArray(tool_calls)) return;

    int count = cJSON_GetArraySize(tool_calls);
    LOG_I("[LLM -> Target] tool_calls count: %d", count);
    for (int i = 0; i < count; i++) {
        const cJSON *call   = cJSON_GetArrayItem(tool_calls, i);
        const char *method  = json_get_string_or_default(call, "method", "");
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(call, "parameters");
        if (!params) {
            LOG_W("AI tool_call missing parameters");
            continue;
        }

        const cJSON *val_item = cJSON_GetObjectItemCaseSensitive(params, "value");
        if (!val_item) {
            LOG_W("AI tool_call missing 'value' in parameters");
            continue;
        }

        execute_single_iot_call(method, val_item, 0);
    }
}

static void cloud_llm_register_iot_capabilities(void) {
    if (!g_agent_client) return;

    int ret = agentSendJson(g_agent_client, k_iot_descriptor_msg);
    LOG_I("[Target -> LLM] Register: msg=%s", k_iot_descriptor_msg);
}

static void on_message(const char *msg, void *user_data) {
    (void)user_data;
    if (msg == NULL) {
        LOG_W("[LLM -> Target] Message Recv: null");
        return;
    }
    // LOG_I("[LLM -> Target] Message Recv: %s", msg);

    cJSON *root = cJSON_Parse(msg);
    if (root == NULL) {
        LOG_W("[LLM -> Target] Message Recv (non-json): %s", msg);
        return;
    }

    const char *type = json_get_string_or_default(root, "type", "");
    if (strcmp(type, "assistant_response") == 0) {
        const char *text     = json_get_string_or_default(root, "text", "");
        const char *asr_text = json_get_string_or_default(root, "asr_text", "");
        int   tts_bytes      = json_get_int_or_default(root, "tts_bytes_length", 0);
        LOG_I("[LLM -> Target] Text: %s", text);
        LOG_D("[LLM -> Target] Meta: asr_text='%s', tts_bytes_length=%d", asr_text, tts_bytes);
        
        // Execute tool_calls (IoT device controls)
        if (strcmp(json_get_string_or_default(root, "intent", ""), "iot_control") == 0) {
            execute_iot_tool_calls(root);
        }
    } else if (strcmp(type, "error") == 0) {
        const char *message = json_get_string_or_default(root, "message", "unknown");
        LOG_E("[LLM -> Target] Error Message: %s", message);
    } else if (strcmp(type, "iot") == 0) {
        const char *event      = json_get_string_or_default(root, "event", "");
        const char *session_id = json_get_string_or_default(root, "session_id", "");
        int   descriptor_count = json_get_int_or_default(root, "descriptor_count", -1);
        LOG_I("[LLM -> Target] IOT Event: event=%s, descriptor_count=%d, session_id=%s",
              event, descriptor_count, session_id);
    } else {
        LOG_I("[LLM -> Target] Message Recv (type=%s)", type[0] ? type : "unknown");
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
    
    // 向主线程推入网络状态更新指令 (CMD_WIFI_STATE - 0x03)  0x01/0x02 为未连接云端
    uint8_t *payload = malloc(1);
    if (payload) {
        payload[0] = (status == AGENT_STATUS_CONNECTED) ? 0x04 : 0x01;

        SystemMsg msg = {.type = MSG_TYPE_TUYA_CMD, .cmd = CMD_WIFI_STATE, .data = payload, .len = 1};
        if (msg_queue_push(&g_sys_queue, &msg) != 0) {
            free(payload);
            LOG_E("Failed to enqueue WIFI_STATE command");
        }
    }
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
    config.ws_url            = "wss://tongqu.zworker.online/ws/v1/chat";
    config.device_id         = device_id;
    config.client_id         = "proai-linux-client";
    config.authorization     = "";
    config.audio_format      = "pcm";
    config.sample_rate       = 16000;
    config.channels          = 1;
    config.frame_duration_ms = 20;
    config.feature_iot       = 1;
    config.feature_speaker   = 1;        // 强制文本模式，不请求 TTS 音频
    config.feature_mcp       = 0;

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
    LOG_I("[Target -> LLM]: '%s' ", text);
    return ret;
}

int cloud_llm_send_json(const char *json_str) {
    if (!g_agent_client) {
        LOG_E("Cannot send JSON: AI Platform client not initialized.");
        return -1;
    }
    int ret = agentSendJson(g_agent_client, json_str);
    LOG_I("[Target -> LLM]: %s", json_str);
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

#ifndef CLOUD_LLM_H
#define CLOUD_LLM_H

#include <stdint.h>
#include <stddef.h>
#include "../../tongqu-sdk/agent_sdk.h"
#include "cJSON.h"

int cloud_llm_init(const char *device_id, const char *device_secret);
int cloud_llm_send_text(const char *text);
int cloud_llm_send_json(const char *json_str);
int cloud_llm_send_audio(const uint8_t *data, size_t len);
void cloud_llm_cleanup(void);

// 供外部 (如 ipc_server) 复用的 DP 解析接口
int execute_single_iot_call(const char *method, const cJSON *val_item, int is_offline_voice);

#endif // CLOUD_LLM_H

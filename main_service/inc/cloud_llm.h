#ifndef CLOUD_LLM_H
#define CLOUD_LLM_H

#include <stdint.h>
#include <stddef.h>
#include "../../tongqu-sdk/agent_sdk.h"

int cloud_llm_init(const char *device_id, const char *device_secret);
int cloud_llm_send_text(const char *text);
int cloud_llm_send_json(const char *json_str);
int cloud_llm_send_audio(const uint8_t *data, size_t len);
void cloud_llm_cleanup(void);

#endif // CLOUD_LLM_H
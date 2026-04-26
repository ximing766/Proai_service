#ifndef CLOUD_LLM_H
#define CLOUD_LLM_H

#include <stdint.h>
#include <stddef.h>

int cloud_llm_init(void);
int cloud_llm_send_text(const char *text);
int cloud_llm_send_audio(const uint8_t *data, size_t len);
void cloud_llm_cleanup(void);

#endif // CLOUD_LLM_H
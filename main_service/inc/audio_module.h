#ifndef _AUDIO_MODULE_H_
#define _AUDIO_MODULE_H_

#include <stddef.h>
#include <stdint.h>

int audio_module_init(void);
int audio_module_play(const uint8_t *data, size_t len);
void audio_module_cleanup(void);

#endif // _AUDIO_MODULE_H_
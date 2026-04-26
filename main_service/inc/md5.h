#ifndef MD5_H
#define MD5_H

#include <stdint.h>

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
} MD5_CTX;

void MD5Init(MD5_CTX *context);
void MD5Update(MD5_CTX *context, const uint8_t *input, unsigned int inputLen);
void MD5Final(uint8_t digest[16], MD5_CTX *context);
void md5_file(const char *filename, uint8_t *digest);

#endif

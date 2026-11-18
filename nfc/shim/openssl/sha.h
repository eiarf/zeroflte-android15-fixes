#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA_DIGEST_LENGTH 20
#define SHA256_DIGEST_LENGTH 32

typedef struct {
    uint32_t state[5];
    uint64_t bitcount;
    uint8_t buffer[64];
} SHA_CTX;

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t buffer[64];
} SHA256_CTX;

int SHA1_Init(SHA_CTX* c);
int SHA1_Update(SHA_CTX* c, const void* data, size_t len);
int SHA1_Final(unsigned char* md, SHA_CTX* c);

int SHA256_Init(SHA256_CTX* c);
int SHA256_Update(SHA256_CTX* c, const void* data, size_t len);
int SHA256_Final(unsigned char* md, SHA256_CTX* c);

#ifdef __cplusplus
}
#endif

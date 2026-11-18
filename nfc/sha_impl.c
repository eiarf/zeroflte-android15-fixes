#include "openssl/sha.h"
#include <string.h>

static inline uint32_t rol32(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }
static inline uint32_t ror32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void sha1_transform(uint32_t st[5], const uint8_t blk[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16) |
               ((uint32_t)blk[i*4+2] << 8) | blk[i*4+3];
    for (int i = 16; i < 80; ++i) w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;            k = 0xCA62C1D6u; }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e;
}

int SHA1_Init(SHA_CTX* c) {
    c->state[0] = 0x67452301u; c->state[1] = 0xEFCDAB89u;
    c->state[2] = 0x98BADCFEu; c->state[3] = 0x10325476u;
    c->state[4] = 0xC3D2E1F0u; c->bitcount = 0;
    return 1;
}

int SHA1_Update(SHA_CTX* c, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    size_t idx = (size_t)((c->bitcount >> 3) & 63);
    c->bitcount += (uint64_t)len << 3;
    if (idx && idx + len >= 64) {
        memcpy(c->buffer + idx, p, 64 - idx);
        sha1_transform(c->state, c->buffer);
        p += 64 - idx; len -= 64 - idx; idx = 0;
    }
    while (len >= 64) { sha1_transform(c->state, p); p += 64; len -= 64; }
    if (len) memcpy(c->buffer + idx, p, len);
    return 1;
}

int SHA1_Final(unsigned char* md, SHA_CTX* c) {
    uint64_t bits = c->bitcount;
    uint8_t pad = 0x80;
    SHA1_Update(c, &pad, 1);
    pad = 0;
    while (((c->bitcount >> 3) & 63) != 56) SHA1_Update(c, &pad, 1);
    uint8_t lenbe[8];
    for (int i = 0; i < 8; ++i) lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
    SHA1_Update(c, lenbe, 8);
    for (int i = 0; i < 5; ++i) {
        md[i*4] = (uint8_t)(c->state[i] >> 24);
        md[i*4+1] = (uint8_t)(c->state[i] >> 16);
        md[i*4+2] = (uint8_t)(c->state[i] >> 8);
        md[i*4+3] = (uint8_t)(c->state[i]);
    }
    return 1;
}

static const uint32_t k256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static void sha256_transform(uint32_t st[8], const uint8_t blk[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16) |
               ((uint32_t)blk[i*4+2] << 8) | blk[i*4+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ror32(w[i-15], 7) ^ ror32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ror32(w[i-2], 17) ^ ror32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=st[0], b=st[1], c=st[2], d=st[3], e=st[4], f=st[5], g=st[6], h=st[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + k256[i] + w[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;
}

int SHA256_Init(SHA256_CTX* c) {
    c->state[0]=0x6a09e667u; c->state[1]=0xbb67ae85u; c->state[2]=0x3c6ef372u; c->state[3]=0xa54ff53au;
    c->state[4]=0x510e527fu; c->state[5]=0x9b05688cu; c->state[6]=0x1f83d9abu; c->state[7]=0x5be0cd19u;
    c->bitcount = 0;
    return 1;
}

int SHA256_Update(SHA256_CTX* c, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    size_t idx = (size_t)((c->bitcount >> 3) & 63);
    c->bitcount += (uint64_t)len << 3;
    if (idx && idx + len >= 64) {
        memcpy(c->buffer + idx, p, 64 - idx);
        sha256_transform(c->state, c->buffer);
        p += 64 - idx; len -= 64 - idx; idx = 0;
    }
    while (len >= 64) { sha256_transform(c->state, p); p += 64; len -= 64; }
    if (len) memcpy(c->buffer + idx, p, len);
    return 1;
}

int SHA256_Final(unsigned char* md, SHA256_CTX* c) {
    uint64_t bits = c->bitcount;
    uint8_t pad = 0x80;
    SHA256_Update(c, &pad, 1);
    pad = 0;
    while (((c->bitcount >> 3) & 63) != 56) SHA256_Update(c, &pad, 1);
    uint8_t lenbe[8];
    for (int i = 0; i < 8; ++i) lenbe[i] = (uint8_t)(bits >> (56 - i * 8));
    SHA256_Update(c, lenbe, 8);
    for (int i = 0; i < 8; ++i) {
        md[i*4] = (uint8_t)(c->state[i] >> 24);
        md[i*4+1] = (uint8_t)(c->state[i] >> 16);
        md[i*4+2] = (uint8_t)(c->state[i] >> 8);
        md[i*4+3] = (uint8_t)(c->state[i]);
    }
    return 1;
}

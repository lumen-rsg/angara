// =============================================================================
// sha256.h — Shared SHA-256 implementation for hash.c and jwt.c (L29)
// =============================================================================
// This header provides a self-contained, static SHA-256 implementation used by
// both the hash module (standalone hashing, HMAC, file hashing) and the JWT
// module (HMAC-SHA256 signature verification). Previously duplicated ~100 lines
// in each .c file.
//
// Usage:
//   #include "sha256.h"
//   SHA256_CTX ctx;
//   sha256_init(&ctx);
//   sha256_update(&ctx, data, len);
//   unsigned char digest[32];
//   sha256_final(digest, &ctx);
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[8];
    uint64_t count;
    unsigned char buffer[64];
} SHA256_CTX;

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x,y,z)  (((x)&(y))^((~(x))&(z)))
#define SHA256_MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define SHA256_SIG0(x) (SHA256_ROTR(x,2)^SHA256_ROTR(x,13)^SHA256_ROTR(x,22))
#define SHA256_SIG1(x) (SHA256_ROTR(x,6)^SHA256_ROTR(x,11)^SHA256_ROTR(x,25))
#define SHA256_sigma0(x) (SHA256_ROTR(x,7)^SHA256_ROTR(x,18)^((x)>>3))
#define SHA256_sigma1(x) (SHA256_ROTR(x,17)^SHA256_ROTR(x,19)^((x)>>10))

static void sha256_transform(uint32_t state[8], const unsigned char block[64]) {
    uint32_t W[64];
    for (int t = 0; t < 16; t++)
        W[t] = (uint32_t)block[t*4]<<24 | (uint32_t)block[t*4+1]<<16 | (uint32_t)block[t*4+2]<<8 | (uint32_t)block[t*4+3];
    for (int t = 16; t < 64; t++)
        W[t] = SHA256_sigma1(W[t-2]) + W[t-7] + SHA256_sigma0(W[t-15]) + W[t-16];

    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
    for (int t = 0; t < 64; t++) {
        uint32_t T1 = h + SHA256_SIG1(e) + SHA256_CH(e,f,g) + sha256_k[t] + W[t];
        uint32_t T2 = SHA256_SIG0(a) + SHA256_MAJ(a,b,c);
        h=g; g=f; f=e; e=d+T1; d=c; c=b; b=a; a=T1+T2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_init(SHA256_CTX* ctx) {
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85; ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c; ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(SHA256_CTX* ctx, const unsigned char* data, size_t len) {
    size_t index = (size_t)(ctx->count % 64);
    ctx->count += len;
    size_t i = 0;
    if (index) {
        size_t part = 64 - index;
        if (len >= part) { memcpy(ctx->buffer + index, data, part); sha256_transform(ctx->state, ctx->buffer); i = part; }
        else { memcpy(ctx->buffer + index, data, len); return; }
    }
    for (; i + 64 <= len; i += 64) sha256_transform(ctx->state, data + i);
    if (i < len) memcpy(ctx->buffer, data + i, len - i);
}

static void sha256_final(unsigned char digest[32], SHA256_CTX* ctx) {
    static unsigned char padding[64] = {0x80};
    unsigned char bits[8];
    for (int i = 0; i < 8; i++) bits[i] = (unsigned char)((ctx->count * 8) >> (i * 8));
    size_t padlen = (ctx->count % 64 < 56) ? (56 - ctx->count % 64) : (120 - ctx->count % 64);
    sha256_update(ctx, padding, padlen);
    sha256_update(ctx, bits, 8);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++)
            digest[i*4+j] = (unsigned char)((ctx->state[i] >> (24 - j*8)) & 0xFF);
}

#ifdef __cplusplus
}
#endif

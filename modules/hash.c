//
// hash.c — Angara hash module (FNV-1a, DJB2, CRC32, MD5, SHA256)
//

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

// --- FNV-1a (64-bit) ---

AngaraObject Angara_hash_fnv1a(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("fnv1a(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return ang_i64((int64_t)hash);
}

// --- DJB2 ---

AngaraObject Angara_hash_djb2(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("djb2(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    uint64_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + data[i]; /* hash * 33 + c */
    }
    return ang_i64((int64_t)hash);
}

// --- CRC32 ---

static uint32_t crc32_table[256];
static int crc32_table_init = 0;

static void init_crc32_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_init = 1;
}

AngaraObject Angara_hash_crc32(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("crc32(data) expects one string argument.");
        return ang_nil();
    }
    if (!crc32_table_init) init_crc32_table();

    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    crc ^= 0xFFFFFFFF;
    return ang_i64((int64_t)crc);
}

// --- MD5 (compact self-contained implementation) ---

typedef struct {
    uint32_t state[4];
    uint64_t count;
    unsigned char buffer[64];
} MD5_CTX;

static const uint32_t md5_T[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};
static const int md5_s[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

static void md5_transform(uint32_t state[4], const unsigned char block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t M[16];
    for (int i = 0; i < 16; i++)
        M[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1]<<8) | ((uint32_t)block[i*4+2]<<16) | ((uint32_t)block[i*4+3]<<24);

    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5*i+1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3*i+5) % 16; }
        else              { f = c ^ (b | ~d);       g = (7*i) % 16; }
        f += a + md5_T[i] + M[g];
        a = d; d = c; c = b;
        b += (f << md5_s[i]) | (f >> (32 - md5_s[i]));
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void md5_init(MD5_CTX* ctx) {
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe; ctx->state[3] = 0x10325476;
    ctx->count = 0;
}

static void md5_update(MD5_CTX* ctx, const unsigned char* data, size_t len) {
    size_t index = (size_t)(ctx->count % 64);
    ctx->count += len;
    size_t i = 0;
    if (index) {
        size_t part = 64 - index;
        if (len >= part) { memcpy(ctx->buffer + index, data, part); md5_transform(ctx->state, ctx->buffer); i = part; }
        else { memcpy(ctx->buffer + index, data, len); return; }
    }
    for (; i + 64 <= len; i += 64) md5_transform(ctx->state, data + i);
    if (i < len) memcpy(ctx->buffer, data + i, len - i);
}

static void md5_final(unsigned char digest[16], MD5_CTX* ctx) {
    static unsigned char padding[64] = {0x80};
    unsigned char bits[8];
    for (int i = 0; i < 8; i++) bits[i] = (unsigned char)((ctx->count * 8) >> (i * 8));
    size_t padlen = (ctx->count % 64 < 56) ? (56 - ctx->count % 64) : (120 - ctx->count % 64);
    md5_update(ctx, padding, padlen);
    md5_update(ctx, bits, 8);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            digest[i*4+j] = (unsigned char)((ctx->state[i] >> (j*8)) & 0xFF);
}

AngaraObject Angara_hash_md5(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("md5(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    MD5_CTX ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    unsigned char digest[16];
    md5_final(digest, &ctx);

    char out[33];
    for (int i = 0; i < 16; i++) snprintf(out + i*2, 3, "%02x", digest[i]);
    return ang_api->string_len(out, 32);
}

// --- SHA-256 (compact self-contained implementation) ---

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

AngaraObject Angara_hash_sha256(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("sha256(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    unsigned char digest[32];
    sha256_final(digest, &ctx);

    char out[65];
    for (int i = 0; i < 32; i++) snprintf(out + i*2, 3, "%02x", digest[i]);
    return ang_api->string_len(out, 64);
}

// --- Export Table ---

static const AngaraFuncDef HASH_EXPORTS[] = {
    {"fnv1a",  Angara_hash_fnv1a,  "s->i", NULL},
    {"djb2",   Angara_hash_djb2,   "s->i", NULL},
    {"crc32",  Angara_hash_crc32,  "s->i", NULL},
    {"md5",    Angara_hash_md5,    "s->s", NULL},
    {"sha256", Angara_hash_sha256, "s->s", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(hash) {
    ang_api = api;
    *def_count = (sizeof(HASH_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return HASH_EXPORTS;
}
/// Angara hash module — FNV-1a, DJB2, CRC32, MD5, SHA1, SHA256, HMAC-SHA256.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "Angara.h"
#include "sha256.h"   // L29: shared SHA-256 implementation (also used by jwt.c)

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)


AngaraObject Angara_hash_fnv1a(int arg_count, AngaraObject* args) {
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return ang_i64((int64_t)hash);
}


AngaraObject Angara_hash_djb2(int arg_count, AngaraObject* args) {
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    uint64_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + data[i];
    }
    return ang_i64((int64_t)hash);
}


static uint32_t crc32_table[256];
static pthread_once_t crc32_once = PTHREAD_ONCE_INIT;

static void init_crc32_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
        crc32_table[i] = crc;
    }
}

AngaraObject Angara_hash_crc32(int arg_count, AngaraObject* args) {
    pthread_once(&crc32_once, init_crc32_table);

    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    crc ^= 0xFFFFFFFF;
    return ang_i64((int64_t)crc);
}


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

AngaraObject Angara_hash_sha256(int arg_count, AngaraObject* args) {
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


AngaraObject Angara_hash_crc16(int arg_count, AngaraObject* args) {
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return ang_i64((int64_t)crc);
}


typedef struct {
    uint32_t state[5];
    uint64_t count;
    unsigned char buffer[64];
} SHA1_CTX;

static void sha1_transform(uint32_t state[5], const unsigned char block[64]) {
    uint32_t W[80];
    for (int t = 0; t < 16; t++)
        W[t] = (uint32_t)block[t*4]<<24 | (uint32_t)block[t*4+1]<<16 | (uint32_t)block[t*4+2]<<8 | (uint32_t)block[t*4+3];
    for (int t = 16; t < 80; t++)
        W[t] = SHA256_ROTR(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16], 31);

    uint32_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4];
    for (int t = 0; t < 80; t++) {
        uint32_t f, k;
        if (t < 20)      { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (t < 40) { f = b ^ c ^ d;          k = 0x6ED9EBA1; }
        else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else              { f = b ^ c ^ d;          k = 0xCA62C1D6; }
        uint32_t temp = SHA256_ROTR(a, 27) + f + e + k + W[t];
        e = d; d = c; c = SHA256_ROTR(b, 2); b = a; a = temp;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

static void sha1_init(SHA1_CTX* ctx) {
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE; ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

static void sha1_update(SHA1_CTX* ctx, const unsigned char* data, size_t len) {
    size_t index = (size_t)(ctx->count % 64);
    ctx->count += len;
    size_t i = 0;
    if (index) {
        size_t part = 64 - index;
        if (len >= part) { memcpy(ctx->buffer + index, data, part); sha1_transform(ctx->state, ctx->buffer); i = part; }
        else { memcpy(ctx->buffer + index, data, len); return; }
    }
    for (; i + 64 <= len; i += 64) sha1_transform(ctx->state, data + i);
    if (i < len) memcpy(ctx->buffer, data + i, len - i);
}

static void sha1_final(unsigned char digest[20], SHA1_CTX* ctx) {
    static unsigned char padding[64] = {0x80};
    unsigned char bits[8];
    for (int i = 0; i < 8; i++) bits[i] = (unsigned char)((ctx->count * 8) >> (i * 8));
    size_t padlen = (ctx->count % 64 < 56) ? (56 - ctx->count % 64) : (120 - ctx->count % 64);
    sha1_update(ctx, padding, padlen);
    sha1_update(ctx, bits, 8);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++)
            digest[i*4+j] = (unsigned char)((ctx->state[i] >> (24 - j*8)) & 0xFF);
}

AngaraObject Angara_hash_sha1(int arg_count, AngaraObject* args) {
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    unsigned char digest[20];
    sha1_final(digest, &ctx);

    char out[41];
    for (int i = 0; i < 20; i++) snprintf(out + i*2, 3, "%02x", digest[i]);
    return ang_api->string_len(out, 40);
}


AngaraObject Angara_hash_hmac_sha256(int arg_count, AngaraObject* args) {
    const unsigned char* data = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t data_len = ang_api->str_len(args[0]);
    const unsigned char* key = (const unsigned char*)ang_api->as_cstr(args[1]);
    size_t key_len = ang_api->str_len(args[1]);

    unsigned char k_pad[64];
    memset(k_pad, 0, 64);

    if (key_len > 64) {
        SHA256_CTX kctx;
        sha256_init(&kctx);
        sha256_update(&kctx, key, key_len);
        sha256_final(k_pad, &kctx);
    } else {
        memcpy(k_pad, key, key_len);
    }

    unsigned char ipad[64];
    for (int i = 0; i < 64; i++) ipad[i] = k_pad[i] ^ 0x36;

    SHA256_CTX inner;
    sha256_init(&inner);
    sha256_update(&inner, ipad, 64);
    sha256_update(&inner, data, data_len);
    unsigned char inner_hash[32];
    sha256_final(inner_hash, &inner);

    unsigned char opad[64];
    for (int i = 0; i < 64; i++) opad[i] = k_pad[i] ^ 0x5c;

    SHA256_CTX outer;
    sha256_init(&outer);
    sha256_update(&outer, opad, 64);
    sha256_update(&outer, inner_hash, 32);
    unsigned char digest[32];
    sha256_final(digest, &outer);

    char out[65];
    for (int i = 0; i < 32; i++) snprintf(out + i*2, 3, "%02x", digest[i]);
    return ang_api->string_len(out, 64);
}


AngaraObject Angara_hash_hash_file(int arg_count, AngaraObject* args) {
    const char* algo = ang_api->as_cstr(args[0]);
    const char* path = ang_api->as_cstr(args[1]);

    FILE* file = fopen(path, "rb");
    if (!file) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "hash_file: cannot open '%s'", path);
        ang_api->throw_error(errbuf);
        return ang_nil();
    }

    unsigned char buf[4096];
    size_t n;

    if (strcmp(algo, "md5") == 0) {
        MD5_CTX ctx;
        md5_init(&ctx);
        while ((n = fread(buf, 1, sizeof(buf), file)) > 0) md5_update(&ctx, buf, n);
        unsigned char digest[16];
        md5_final(digest, &ctx);
        fclose(file);
        char out[33];
        for (int i = 0; i < 16; i++) snprintf(out + i*2, 3, "%02x", digest[i]);
        return ang_api->string_len(out, 32);
    } else if (strcmp(algo, "sha1") == 0) {
        SHA1_CTX ctx;
        sha1_init(&ctx);
        while ((n = fread(buf, 1, sizeof(buf), file)) > 0) sha1_update(&ctx, buf, n);
        unsigned char digest[20];
        sha1_final(digest, &ctx);
        fclose(file);
        char out[41];
        for (int i = 0; i < 20; i++) snprintf(out + i*2, 3, "%02x", digest[i]);
        return ang_api->string_len(out, 40);
    } else if (strcmp(algo, "sha256") == 0) {
        SHA256_CTX ctx;
        sha256_init(&ctx);
        while ((n = fread(buf, 1, sizeof(buf), file)) > 0) sha256_update(&ctx, buf, n);
        unsigned char digest[32];
        sha256_final(digest, &ctx);
        fclose(file);
        char out[65];
        for (int i = 0; i < 32; i++) snprintf(out + i*2, 3, "%02x", digest[i]);
        return ang_api->string_len(out, 64);
    } else {
        fclose(file);
        ang_api->throw_error("hash_file: unknown algorithm. Use 'md5', 'sha1', or 'sha256'.");
        return ang_nil();
    }
}


static const AngaraFuncDef HASH_EXPORTS[] = {
    {"fnv1a",        Angara_hash_fnv1a,        "s->i",   NULL},
    {"djb2",         Angara_hash_djb2,         "s->i",   NULL},
    {"crc16",        Angara_hash_crc16,        "s->i",   NULL},
    {"crc32",        Angara_hash_crc32,        "s->i",   NULL},
    {"md5",          Angara_hash_md5,          "s->s",   NULL},
    {"sha1",         Angara_hash_sha1,         "s->s",   NULL},
    {"sha256",       Angara_hash_sha256,       "s->s",   NULL},
    {"hmac_sha256",  Angara_hash_hmac_sha256,  "ss->s",  NULL},
    {"hash_file",    Angara_hash_hash_file,    "ss->s",  NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(hash) {
    ang_api = api;
    *def_count = (sizeof(HASH_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return HASH_EXPORTS;
}

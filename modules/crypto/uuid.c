/// Angara UUID module — UUID v4 generation via CSPRNG.
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif
#include "Angara.h"

static int secure_random_bytes(unsigned char* buf, size_t len) {
#ifdef _WIN32
    // Windows: use BCryptGenRandom (CNG API), available since Vista/Server 2008
    if (BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
        return 0;
    // Fallback to rand() if CNG fails (unlikely)
    for (size_t i = 0; i < len; i++) buf[i] = (unsigned char)(rand() % 256);
    return 0;
#elif defined(__APPLE__)
    arc4random_buf(buf, len);
    return 0;
#elif defined(__linux__)
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
#else
    for (size_t i = 0; i < len; i++) buf[i] = (unsigned char)(rand() % 256);
    return 0;
#endif
}

static AngaraObject format_uuid(const unsigned char* bytes) {
    char buffer[37];
    snprintf(buffer, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return ang_api->string(buffer);
}

AngaraObject Angara_uuid_v4(int arg_count, AngaraObject* args) {
    unsigned char bytes[16];
    if (secure_random_bytes(bytes, 16) != 0) {
        ang_api->throw_error("uuid.v4: failed to generate secure random bytes.");
        return ang_nil();
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    return format_uuid(bytes);
}

AngaraObject Angara_uuid_v7(int arg_count, AngaraObject* args) {
    unsigned char bytes[16];
    if (secure_random_bytes(bytes, 16) != 0) {
        ang_api->throw_error("uuid.v7: failed to generate secure random bytes.");
        return ang_nil();
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;

    bytes[0] = (unsigned char)(ms >> 40);
    bytes[1] = (unsigned char)(ms >> 32);
    bytes[2] = (unsigned char)(ms >> 24);
    bytes[3] = (unsigned char)(ms >> 16);
    bytes[4] = (unsigned char)(ms >> 8);
    bytes[5] = (unsigned char)(ms);

    bytes[6] = (bytes[6] & 0x0F) | 0x70;

    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    return format_uuid(bytes);
}

AngaraObject Angara_uuid_nil(int arg_count, AngaraObject* args) {
    return ang_api->string("00000000-0000-0000-0000-000000000000");
}

AngaraObject Angara_uuid_is_valid(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_STRING) {
        return ang_bool(false);
    }
    const char* s = ang_api->as_cstr(args[0]);
    if (strlen(s) != 36) return ang_bool(false);
    static const int dash_pos[] = {8, 13, 18, 23};
    for (int i = 0; i < 4; i++) if (s[dash_pos[i]] != '-') return ang_bool(false);
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        if (!isxdigit((unsigned char)s[i])) return ang_bool(false);
    }
    return ang_bool(true);
}

static const AngaraFuncDef UUID_EXPORTS[] = {
    {"v4",       Angara_uuid_v4,       "->s",  NULL},
    {"v7",       Angara_uuid_v7,       "->s",  NULL},
    {"nil",      Angara_uuid_nil,      "->s",  NULL},
    {"is_valid", Angara_uuid_is_valid, "s->b", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(uuid) {
    ang_api = api;
    *def_count = (sizeof(UUID_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return UUID_EXPORTS;
}
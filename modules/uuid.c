//
// uuid.c — Angara UUID module (rewritten for 16-byte ABI + vtable)
//

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "Angara.h"

// uuid.v4() -> string
AngaraObject Angara_uuid_v4(int arg_count, AngaraObject* args) {
    static bool seeded = false;
    if (!seeded) { srand((unsigned int)time(NULL)); seeded = true; }

    unsigned char bytes[16];
    for (int i = 0; i < 16; i++) bytes[i] = rand() % 256;
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    char buffer[37];
    snprintf(buffer, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);

    return ang_api->string(buffer);
}

static const AngaraFuncDef UUID_EXPORTS[] = {
    {"v4", Angara_uuid_v4, "->s", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(uuid) {
    ang_api = api;
    *def_count = (sizeof(UUID_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return UUID_EXPORTS;
}
//
// Created by cv2 on 11.12.2025.
//

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "../runtime/angara_runtime.h"

// Simple pseudo-random UUID v4 generator
AngaraObject Angara_uuid_v4(int arg_count, AngaraObject* args) {
    char buffer[37]; // 36 chars + null terminator

    // Seed once (simple check)
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }

    // Generate random bytes
    unsigned char bytes[16];
    for (int i = 0; i < 16; i++) {
        bytes[i] = rand() % 256;
    }

    // Version 4
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    // Variant 1
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    snprintf(buffer, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5],
        bytes[6], bytes[7],
        bytes[8], bytes[9],
        bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]
    );

    return angara_create_string(buffer);
}

static const AngaraFuncDef UUID_EXPORTS[] = {
    {"v4", Angara_uuid_v4, "->s", NULL},
    {NULL, NULL, NULL, NULL}
};

ANGARA_MODULE_INIT(uuid) {
    *def_count = 1;
    return UUID_EXPORTS;
}
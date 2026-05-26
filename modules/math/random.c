/// Angara random module — xoshiro256** PRNG with native instance state.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "Angara.h"


typedef struct {
    uint64_t s[4];
} RandomState;

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256ss(RandomState* st) {
    const uint64_t result = rotl(st->s[1] * 5, 7) * 9;
    const uint64_t t = st->s[1] << 17;
    st->s[2] ^= st->s[0];
    st->s[3] ^= st->s[1];
    st->s[1] ^= st->s[2];
    st->s[0] ^= st->s[3];
    st->s[2] ^= t;
    st->s[3] = rotl(st->s[3], 45);
    return result;
}

static uint64_t splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void seed_state(RandomState* st, uint64_t seed) {
    uint64_t sp = seed;
    st->s[0] = splitmix64(&sp);
    st->s[1] = splitmix64(&sp);
    st->s[2] = splitmix64(&sp);
    st->s[3] = splitmix64(&sp);
}

static void finalize_random(void* data) { free(data); }

static RandomState global_rng;
static int global_seeded = 0;

static void ensure_global_seeded(void) {
    if (!global_seeded) {
        uint64_t seed = (uint64_t)time(NULL) ^ ((uint64_t)&global_rng >> 4);
        seed_state(&global_rng, seed);
        global_seeded = 1;
    }
}

#define IS_RNG(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_NATIVE_INSTANCE)


AngaraObject Angara_random_int(int arg_count, AngaraObject* args) {
    ensure_global_seeded();
    return ang_i64((int64_t)xoshiro256ss(&global_rng));
}

AngaraObject Angara_random_int_range(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_i64(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("int_range(min, max) expects two i64 arguments.");
        return ang_nil();
    }
    int64_t lo = ang_as_i64(args[0]);
    int64_t hi = ang_as_i64(args[1]);
    if (lo > hi) { ang_api->throw_error("int_range: min must be <= max."); return ang_nil(); }
    ensure_global_seeded();
    uint64_t range = (uint64_t)(hi - lo + 1);
    uint64_t val = xoshiro256ss(&global_rng);
    return ang_i64(lo + (int64_t)(val % range));
}

AngaraObject Angara_random_float(int arg_count, AngaraObject* args) {
    ensure_global_seeded();
    uint64_t val = xoshiro256ss(&global_rng);
    double d = (double)(val >> 11) / (double)(1ULL << 53);
    return ang_f64(d);
}

AngaraObject Angara_random_bool(int arg_count, AngaraObject* args) {
    ensure_global_seeded();
    return ang_bool(xoshiro256ss(&global_rng) & 1);
}

AngaraObject Angara_random_seed(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("seed(value) expects one i64 argument.");
        return ang_nil();
    }
    seed_state(&global_rng, (uint64_t)ang_as_i64(args[0]));
    global_seeded = 1;
    return ang_nil();
}

AngaraObject Angara_random_bytes(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("bytes(n) expects one i64 argument.");
        return ang_nil();
    }
    int64_t n = ang_as_i64(args[0]);
    if (n < 0) { ang_api->throw_error("bytes: n must be >= 0."); return ang_nil(); }
    if (n == 0) return ang_api->string("");

    ensure_global_seeded();
    char* buf = (char*)malloc((size_t)n);
    if (!buf) { ang_api->throw_error("bytes: out of memory."); return ang_nil(); }

    for (int64_t i = 0; i < n; i++) {
        buf[i] = (char)(xoshiro256ss(&global_rng) & 0xFF);
    }
    return ang_api->string_no_copy(buf, (size_t)n);
}

AngaraObject Angara_random_pick(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0])) {
        ang_api->throw_error("pick(list) expects a list argument.");
        return ang_nil();
    }
    if (ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("pick(list) expects a list argument.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    if (len == 0) { ang_api->throw_error("pick: cannot pick from empty list."); return ang_nil(); }
    ensure_global_seeded();
    uint64_t idx = xoshiro256ss(&global_rng) % (uint64_t)len;
    return ang_api->list_get(args[0], (int64_t)idx);
}

AngaraObject Angara_random_shuffle(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0])) {
        ang_api->throw_error("shuffle(list) expects a list argument.");
        return ang_nil();
    }
    if (ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("shuffle(list) expects a list argument.");
        return ang_nil();
    }
    ensure_global_seeded();
    size_t len = ang_api->list_len(args[0]);
    for (size_t i = len - 1; i > 0; i--) {
        uint64_t j = xoshiro256ss(&global_rng) % (uint64_t)(i + 1);
        AngaraObject a = ang_api->list_get(args[0], (int64_t)i);
        AngaraObject b = ang_api->list_get(args[0], (int64_t)j);
        ang_api->list_set(args[0], (int64_t)i, b);
        ang_api->list_set(args[0], (int64_t)j, a);
        ang_api->decref(a);
        ang_api->decref(b);
    }
    return ang_nil();
}

AngaraObject Angara_random_uuid_v4(int arg_count, AngaraObject* args) {
    ensure_global_seeded();
    unsigned char bytes[16];
    for (int i = 0; i < 16; i += 8) {
        uint64_t val = xoshiro256ss(&global_rng);
        memcpy(bytes + i, &val, 8);
    }
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


static const AngaraFuncDef RANDOM_EXPORTS[] = {
    {"int",       Angara_random_int,       "->i",      NULL},
    {"int_range", Angara_random_int_range, "ii->i",    NULL},
    {"float",     Angara_random_float,     "->d",      NULL},
    {"bool",      Angara_random_bool,      "->b",      NULL},
    {"seed",      Angara_random_seed,      "i->n",     NULL},
    {"bytes",     Angara_random_bytes,     "i->s",     NULL},
    {"pick",      Angara_random_pick,      "l<a>->a",  NULL},
    {"shuffle",   Angara_random_shuffle,   "l<a>->n",  NULL},
    {"uuid_v4",   Angara_random_uuid_v4,   "->s",      NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(random) {
    ang_api = api;
    *def_count = (sizeof(RANDOM_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return RANDOM_EXPORTS;
}
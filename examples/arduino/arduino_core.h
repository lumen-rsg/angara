// Angara Arduino C Shim — Header
// Bridges the Arduino SDK's setup()/loop() convention with Angara's compiled functions.
//
// AngaraObject memory layout (matches LLVM backend):
//   { int32_t tag, int64_t payload } = 16 bytes
//
// The C shim is the true entry point. Arduino's main() calls setup() once,
// then loop() repeatedly. We forward these to Angara-generated functions.

#ifndef ANGARA_ARDUINO_CORE_H
#define ANGARA_ARDUINO_CORE_H

#include <stdint.h>

#ifdef __AVR__
#include <Arduino.h>
#endif

// AngaraObject must match the LLVM backend's struct layout exactly.
// On AVR: { i32 tag, i64 payload } = 12 bytes (no alignment padding on AVR)
// On ARM: { i32 tag, i64 payload } = 16 bytes (aligned)
typedef struct {
    int32_t tag;
    int64_t payload;
} AngaraObject;

// Tag constants — must match RuntimeBuilder.h
#define ANG_TAG_NIL  0
#define ANG_TAG_BOOL 1
#define ANG_TAG_I64  2
#define ANG_TAG_F64  3
#define ANG_TAG_STR  4
#define ANG_TAG_LIST 5
#define ANG_TAG_OBJ  6

// --- Inline AngaraObject constructors (C equivalents of LLVM backend) ---

static inline AngaraObject ang_nil(void) {
    AngaraObject o;
    o.tag = ANG_TAG_NIL;
    o.payload = 0;
    return o;
}

static inline AngaraObject ang_bool(int8_t val) {
    AngaraObject o;
    o.tag = ANG_TAG_BOOL;
    o.payload = val ? 1 : 0;
    return o;
}

static inline AngaraObject ang_i64(int64_t val) {
    AngaraObject o;
    o.tag = ANG_TAG_I64;
    o.payload = val;
    return o;
}

static inline AngaraObject ang_f64(double val) {
    AngaraObject o;
    o.tag = ANG_TAG_F64;
    // Bitcast double → int64 (same as LLVM backend)
    union { double f; int64_t i; } u;
    u.f = val;
    o.payload = u.i;
    return o;
}

// --- Inline extractors ---

static inline int64_t ang_get_i64(AngaraObject o) {
    return o.payload;
}

static inline int8_t ang_get_bool(AngaraObject o) {
    return o.payload != 0;
}

static inline double ang_get_f64(AngaraObject o) {
    union { double f; int64_t i; } u;
    u.i = o.payload;
    return u.f;
}

#endif // ANGARA_ARDUINO_CORE_H
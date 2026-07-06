// =============================================================================
// Angara.h — Public API for Native Module Authors
// =============================================================================
//
// Every value in Angara is an AngaraObject — a 16-byte tagged union:
//   { i32 tag, i64 payload }
//
// Inline values (nil, bool, i64, f64) store data directly in the payload.
// Heap objects (string, list, record, …) store a pointer as ptrtoint.
//
// Modules receive a vtable (AngaraAPI*) at init time providing all runtime
// operations. Inline constructors and type checks are zero-overhead macros.
//
// Quick Start:
//   1. #include "Angara.h"
//   2. Use ang_nil(), ang_bool(), ang_i64(), ang_f64() to create return values
//   3. Use ang_is_*(v) to check types, ang_as_*(v) to extract C values
//   4. Call api->string("hello"), api->list_push(l, v), etc. for heap ops
//   5. Use ANGARA_MODULE_INIT(name) to define your module entry point
//
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// §1  Core Value Type (16 bytes — matches LLVM ABI)
// =============================================================================

/// The universal Angara value.  Every expression evaluates to one of these.
typedef struct AngaraObject {
    int32_t _tag;       // ANG_TAG_*
    int64_t _payload;   // inline value or ptrtoint(heap_object)
} AngaraObject;

// --- Type tags (must match RuntimeBuilder TAG_* constants) ---
#define ANG_TAG_NIL   0
#define ANG_TAG_BOOL  1
#define ANG_TAG_I64   2
#define ANG_TAG_F64   3
#define ANG_TAG_OBJ   4

// --- Heap object type tags (stored in Object header, must match OBJ_* in RuntimeBuilder) ---
#define ANG_OBJ_STRING          0
#define ANG_OBJ_LIST            1
#define ANG_OBJ_RECORD          2
#define ANG_OBJ_EXCEPTION       3
#define ANG_OBJ_THREAD          4
#define ANG_OBJ_MUTEX           5
#define ANG_OBJ_CLOSURE         6
#define ANG_OBJ_CLASS           7
#define ANG_OBJ_INSTANCE        8
#define ANG_OBJ_NATIVE_INSTANCE 9
#define ANG_OBJ_DATA_INSTANCE  10
#define ANG_OBJ_ENUM_INSTANCE  11
#define ANG_OBJ_BOUND_METHOD   12
#define ANG_OBJ_TRAIT_OBJECT   13  // TS-1: trait/contract interface view
#define ANG_OBJ_RAW_ARRAY      14  // SIMD-1: unboxed dynamic array (f64[], i64[], ...)

// =============================================================================
// §2  Inline Constructors (zero overhead — no runtime call)
// =============================================================================

/// Create a nil value.
static inline AngaraObject ang_nil(void) {
    return (AngaraObject){ ANG_TAG_NIL, 0 };
}

/// Create a boolean value.
static inline AngaraObject ang_bool(bool v) {
    return (AngaraObject){ ANG_TAG_BOOL, v ? 1 : 0 };
}

/// Create a 64-bit integer value.
static inline AngaraObject ang_i64(int64_t v) {
    return (AngaraObject){ ANG_TAG_I64, v };
}

/// Create a 64-bit float value (bitcast double → i64 payload).
static inline AngaraObject ang_f64(double v) {
    AngaraObject o;
    o._tag = ANG_TAG_F64;
    memcpy(&o._payload, &v, sizeof(double));
    return o;
}

// =============================================================================
// §3  Inline Type Checks & Value Extractors (zero overhead)
// =============================================================================

// --- Type checks ---
#define ang_is_nil(v)    ((v)._tag == ANG_TAG_NIL)
#define ang_is_bool(v)   ((v)._tag == ANG_TAG_BOOL)
#define ang_is_i64(v)    ((v)._tag == ANG_TAG_I64)
#define ang_is_f64(v)    ((v)._tag == ANG_TAG_F64)
#define ang_is_obj(v)    ((v)._tag == ANG_TAG_OBJ)

// --- Inline value extraction ---
static inline bool    ang_as_bool(AngaraObject v) { return v._payload != 0; }
static inline int64_t ang_as_i64(AngaraObject v)  { return v._payload; }

/// Extract a double from an f64 AngaraObject (bitcast i64 payload → double).
static inline double ang_as_f64(AngaraObject v) {
    double d;
    memcpy(&d, &v._payload, sizeof(double));
    return d;
}

// =============================================================================
// §4  Runtime API VTable (provided to modules at init time)
// =============================================================================
//
// Module authors access this via the `api` pointer injected by
// ANGARA_MODULE_INIT.  Example: api->string("hello")
// =============================================================================

typedef struct AngaraAPI AngaraAPI;

struct AngaraAPI {

    // --- String operations ---
    AngaraObject (*string)(const char* s);                  ///< Create string from C string (copies)
    AngaraObject (*string_len)(const char* s, size_t len);  ///< Create string with explicit length (copies)
    AngaraObject (*string_no_copy)(char* s, size_t len);    ///< Create string taking ownership of buffer (no copy!)
    AngaraObject (*string_concat)(AngaraObject a, AngaraObject b); ///< Concatenate two strings
    const char*  (*as_cstr)(AngaraObject str);              ///< Get C string pointer (borrowed, do not free)
    size_t       (*str_len)(AngaraObject str);              ///< Get string length

    // --- List operations ---
    AngaraObject (*list_new)(void);                          ///< Create empty list
    void         (*list_push)(AngaraObject list, AngaraObject val); ///< Append value
    AngaraObject (*list_get)(AngaraObject list, int64_t idx);///< Get element by index (nil if OOB)
    void         (*list_set)(AngaraObject list, int64_t idx, AngaraObject val); ///< Set element
    size_t       (*list_len)(AngaraObject list);             ///< Get element count

    // --- Record operations ---
    AngaraObject (*record_new)(void);                                             ///< Create empty record
    AngaraObject (*record_new_with_fields)(int count, AngaraObject* kv_pairs);    ///< Create record with initial key-value pairs
    void         (*record_set)(AngaraObject rec, const char* key, AngaraObject val); ///< Set key=value
    AngaraObject (*record_get)(AngaraObject rec, const char* key);               ///< Get by key (nil if missing)
    size_t       (*record_len)(AngaraObject rec);                               ///< Get entry count
    const char*  (*record_key_at)(AngaraObject rec, size_t idx);                ///< Get key at index
    AngaraObject (*record_val_at)(AngaraObject rec, size_t idx);                ///< Get value at index

    // --- Native Instance operations ---
    AngaraObject (*native_instance_new)(void* data, void (*finalize)(void*), const char* name); ///< Create opaque native instance
    void*        (*native_instance_data)(AngaraObject obj);  ///< Extract the opaque data pointer

    // --- Memory management ---
    void (*incref)(AngaraObject val);   ///< Pin a heap object (prevents collection; paired with decref)
    void (*decref)(AngaraObject val);   ///< Unpin a heap object (may become collectable)

    // --- Conversions ---
    AngaraObject (*to_string)(AngaraObject val);   ///< Convert any value to its string representation

    // --- Truthiness & Equality ---
    bool         (*truthy)(AngaraObject val);       ///< Is the value truthy?
    bool         (*equals)(AngaraObject a, AngaraObject b); ///< Deep equality check

    // --- Error reporting ---
    __attribute__((__noreturn__)) void (*throw_error)(const char* msg); ///< Throw an Angara exception (does not return)

    // --- Function calling ---
    AngaraObject (*call)(AngaraObject fn, int argc, AngaraObject* argv); ///< Call an Angara closure/function from C

    // --- Type introspection ---
    int32_t (*obj_type)(AngaraObject obj);  ///< Get heap object type tag (ANG_OBJ_*)

    // --- Future operations (LIB-4 Stage S-2) ---
    int32_t      (*future_state)(AngaraObject future);       ///< Returns frame.state (-1=resolved)
    AngaraObject (*future_result)(AngaraObject future);      ///< Returns frame.result
    void         (*future_set_loop)(AngaraObject future, void* loop);  ///< Sets frame.loop
};

// =============================================================================
// §5  Module ABI — Export Table
// =============================================================================
//
// Modules define an array of AngaraFuncDef describing their exports.
// The type_string uses a compact DSL: "is->n" means (i64, string) → nil
//
// Type codes:
//   i  = i64          s  = string       b  = bool
//   d  = f64          n  = nil          a  = any
//   l<T> = list<T>    {}  = record
//
// Example: "aas->b" means (any, any, string) → bool
// =============================================================================

/// Function signature for native module functions.
typedef AngaraObject (*AngaraGlobalFn)(int argc, AngaraObject* args);
typedef AngaraObject (*AngaraMethodFn)(int argc, AngaraObject* args);

/// Describes an exported global function or constructor.
typedef struct AngaraFuncDef {
    const char*         name;          ///< Angara-visible function name
    AngaraGlobalFn      function;      ///< C function pointer
    const char*         type_string;   ///< Type signature DSL
    const struct AngaraClassDef* constructs;  ///< Non-NULL if this is a constructor
} AngaraFuncDef;

/// Describes a method on a native class.
typedef struct AngaraMethodDef {
    const char*     name;          ///< Method name
    AngaraMethodFn  function;      ///< C function pointer
    const char*     type_string;   ///< Type signature DSL
} AngaraMethodDef;

/// Describes a field on a native class.
typedef struct AngaraFieldDef {
    const char* name;          ///< Field name
    const char* type_string;   ///< Type signature DSL
    bool        is_const;      ///< Read-only?
} AngaraFieldDef;

/// Describes a native class with fields and methods.
typedef struct AngaraClassDef {
    const char*            name;      ///< Class name
    const AngaraFieldDef*  fields;    ///< NULL-terminated array
    const AngaraMethodDef* methods;   ///< NULL-terminated array
} AngaraClassDef;

// =============================================================================
// §6  Module Entry Point Macro
// =============================================================================
//
// Usage:
//   ANGARA_MODULE_INIT(my_module) {
//       *def_count = (sizeof(my_exports) / sizeof(AngaraFuncDef)) - 1;
//       return my_exports;
//   }
//
// Inside the init function, `api` is available as a `const AngaraAPI*`.
// Store it globally — it remains valid for the lifetime of the process.
// =============================================================================

/// Global API pointer — set once during module init.
static const AngaraAPI* ang_api;

/// Define a module entry point. The module loader calls this at dlopen time.
/// \param name  The module name (must match the library filename, e.g. "io")
#define ANGARA_MODULE_INIT(name) \
    const AngaraFuncDef* Angara_##name##_Init(int* def_count, const AngaraAPI* api)

/// Sentinel for the end of an AngaraFuncDef array.
#define ANGARA_FUNC_END { NULL, NULL, NULL, NULL }

#ifdef __cplusplus
}
#endif
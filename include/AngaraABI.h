// angara.h - The Official Angara C ABI
#ifndef ANGARA_NATIVE_H
#define ANGARA_NATIVE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Core Angara Value Representation ---
// This section provides the fundamental data structures so module
// authors can correctly create and interpret Angara values.

typedef enum {
    VAL_NIL, VAL_BOOL, VAL_I64, VAL_F64, VAL_OBJ
} AngaraValueType;

typedef enum {
    OBJ_STRING, OBJ_LIST, OBJ_RECORD, OBJ_CLOSURE, OBJ_CLASS,
    OBJ_INSTANCE, OBJ_THREAD, OBJ_MUTEX
} ObjectType;

typedef struct Object { ObjectType type; size_t ref_count; } Object;

typedef struct {
    AngaraValueType type;
    union {
        bool boolean;
        int64_t i64;
        double f64;
        Object* obj;
    } as;
} AngaraObject;

// Provide definitions for structs that modules might need to inspect.
typedef struct {
    Object obj;
    size_t length;
    char* chars;
} AngaraString;


// --- C ABI Function Signature ---
// Every native function exposed to Angara MUST have this signature.
typedef AngaraObject (*AngaraNativeFn)(int arg_count, AngaraObject* args);


// --- API Provided by the Angara Host ---
// These are the functions a module can call. They are exported by the
// main Angara executable or the core Angara runtime library.

AngaraObject create_nil(void);
AngaraObject create_bool(bool value);
AngaraObject create_i64(int64_t value);
AngaraObject create_f64(double value);
AngaraObject create_string(const char* chars);
AngaraObject angara_create_string_no_copy(char* owned_chars, size_t length);
void angara_throw_error(const char* message);


// --- Helper Macros for Module Developers ---
#define ANGARA_IS_NIL(value)     ((value).type == VAL_NIL)
#define ANGARA_IS_BOOL(value)    ((value).type == VAL_BOOL)
#define ANGARA_IS_I64(value)     ((value).type == VAL_I64)
#define ANGARA_IS_F64(value)     ((value).type == VAL_F64)
#define ANGARA_IS_STRING(value)  ((value).type == VAL_OBJ && ((Object*)((value).as.obj))->type == OBJ_STRING)

#define ANGARA_AS_BOOL(value)    ((value).as.boolean)
#define ANGARA_AS_I64(value)     ((value).as.i64)
#define ANGARA_AS_F64(value)     ((value).as.f64)
#define ANGARA_AS_CSTRING(value) (((AngaraString*)(value).as.obj)->chars)


// --- API Provided by the Module ---
// This section defines the contract the module must fulfill.

typedef struct {
    const char* name;
    AngaraNativeFn function;
    int arity; // Use -1 for variadic functions
} AngaraFuncDef;

// Helper macros for token pasting
#define ANGARA_ABI_PASTE_IMPL(a, b) a##b
#define ANGARA_ABI_PASTE(a, b) ANGARA_ABI_PASTE_IMPL(a, b)

// The official macro for defining the module's entry point.
// Example: `ANGARA_MODULE_INIT(fs)` expands to:
// `const AngaraFuncDef* Angara_fs_Init(int* def_count)`
#define ANGARA_MODULE_INIT(MODULE_NAME) \
    const AngaraFuncDef* ANGARA_ABI_PASTE(Angara_, ANGARA_ABI_PASTE(MODULE_NAME, _Init))(int* def_count)

#endif // ANGARA_NATIVE_H
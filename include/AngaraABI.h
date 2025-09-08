// angara.h - The Official Angara C ABI

#ifndef ANGARA_NATIVE_H
#define ANGARA_NATIVE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// This file defines the exact same AngaraObject struct as our internal runtime.
// By keeping them identical, we can pass values across the ABI boundary with zero overhead.

// --- Core Angara Value Representation ---

typedef enum {
    VAL_NIL, VAL_BOOL, VAL_I64, VAL_F64, VAL_OBJ
} AngaraValueType;

typedef enum {
    OBJ_STRING, OBJ_LIST, OBJ_RECORD, OBJ_CLOSURE, OBJ_CLASS,
    OBJ_INSTANCE, OBJ_THREAD, OBJ_MUTEX, OBJ_NATIVE_CLOSURE
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

// --- C ABI Function Signature ---
// Every native function exposed to Angara MUST have this signature.
// It receives an array of arguments and returns a single AngaraObject.
typedef AngaraObject (*AngaraNativeFn)(int arg_count, AngaraObject* args);


// --- API Provided by the Angara Host ---
// The module can call these functions, which are provided by the core Angara runtime.
// A module developer would link their .so against a small `libangara_core.so`
// that exports these symbols.

// Value constructors
AngaraObject angara_create_nil(void);
AngaraObject angara_create_bool(bool value);
AngaraObject angara_create_i64(int64_t value);
AngaraObject angara_create_f64(double value);
AngaraObject angara_create_string(const char* chars);

// Value accessors (macros are often better for this)
#define ANGARA_AS_BOOL(value)   ((value).as.boolean)
#define ANGARA_AS_I64(value)    ((value).as.i64)
#define ANGARA_AS_F64(value)    ((value).as.f64)
#define ANGARA_AS_CSTRING(value) (((AngaraString*)(value).as.obj)->chars) // Example

// A function to signal a runtime error from within a native function.
void angara_throw_error(const char* message);


// --- API Provided by the Module ---
// A struct that the module uses to define a single exported function.
typedef struct {
    const char* name;
    AngaraNativeFn function;
    int arity; // -1 for variadic
} AngaraFuncDef;

// The single, C-style entry point that every Angara native module MUST export.
// Its job is to return an array of function definitions.
// It must also return the number of definitions in the array.
const AngaraFuncDef* AngaraModule_Init(int* def_count);

#endif // ANGARA_NATIVE_H
// angara.h - The Official Angara C ABI

#ifndef ANGARA_NATIVE_H
#define ANGARA_NATIVE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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


// --- NEW: Public Definitions for Core Object Structs ---
// These definitions must exactly match the internal definitions in angara_runtime.h
typedef struct {
    Object obj;
    size_t length;
    char* chars;
} AngaraString;

typedef struct {
    Object obj;
    size_t count;
    size_t capacity;
    AngaraObject* elements;
} AngaraList;
// --- END OF NEW ---


// --- C ABI Function Signature ---
typedef AngaraObject (*AngaraNativeFn)(int arg_count, AngaraObject* args);


// --- API Provided by the Angara Host ---
// (These are functions exported by the main Angara executable or libangara_core)
AngaraObject angara_create_nil(void);
AngaraObject angara_create_bool(bool value);
AngaraObject angara_create_i64(int64_t value);
AngaraObject angara_create_f64(double value);
AngaraObject angara_create_string_no_copy(char* chars, size_t length); // For efficiency
void angara_throw_error(const char* message);

// --- Helper Macros ---
#define ANGARA_IS_STRING(value) ((value).type == VAL_OBJ && ((Object*)(value).as.obj)->type == OBJ_STRING)
#define ANGARA_AS_CSTRING(value) (((AngaraString*)(value).as.obj)->chars)


// --- API Provided by the Module ---
typedef struct {
    const char* name;
    AngaraNativeFn function;
    int arity;
} AngaraFuncDef;

const AngaraFuncDef* AngaraModule_Init(int* def_count);

#endif // ANGARA_NATIVE_H
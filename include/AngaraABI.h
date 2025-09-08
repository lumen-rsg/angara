// angara.h - The Official Angara C ABI

#ifndef ANGARA_NATIVE_H
#define ANGARA_NATIVE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Core Angara Value Representation --
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


// --- Public Definitions for Core Object Structs ---
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


// --- C ABI Function Signature ---
typedef AngaraObject (*AngaraNativeFn)(int arg_count, AngaraObject* args);


// --- API Provided by the Angara Host ---
AngaraObject create_nil(void);
AngaraObject create_bool(bool value);
AngaraObject create_i64(int64_t value);
AngaraObject create_f64(double value);
AngaraObject angara_string_from_c(const char* chars);
void angara_throw_error(const char* message);
AngaraObject angara_create_string_no_copy(char* chars, size_t length);
AngaraObject angara_equals(AngaraObject a, AngaraObject b);


// --- Helper Macros ---
#define ANGARA_IS_STRING(value) ((value).type == VAL_OBJ && ((Object*)(value).as.obj)->type == OBJ_STRING)
#define ANGARA_AS_CSTRING(value) (((AngaraString*)(value).as.obj)->chars)

#define ANGARA_ABI_PASTE_IMPL(a, b) a##b
#define ANGARA_ABI_PASTE(a, b) ANGARA_ABI_PASTE_IMPL(a, b)

// The official macro for defining the module's entry point.
//  `ANGARA_MODULE_INIT(fs)` which expands to:
// `const AngaraFuncDef* Angara_fs_Init(int* def_count)`

#define ANGARA_MODULE_INIT(MODULE_NAME) \
    const AngaraFuncDef* ANGARA_ABI_PASTE(Angara_, ANGARA_ABI_PASTE(MODULE_NAME, _Init))(int* def_count)


// --- API Provided by the Module ---
typedef struct {
    const char* name;
    AngaraNativeFn function;
    int arity;
} AngaraFuncDef;

const AngaraFuncDef* AngaraModule_Init(int* def_count);

#endif // ANGARA_NATIVE_H
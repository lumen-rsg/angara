#ifndef ANGARA_RUNTIME_H
#define ANGARA_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --- Core Object Representation ---

// An enum to identify the type of heap-allocated objects
typedef enum {
    OBJ_STRING,
    OBJ_LIST,
    OBJ_RECORD,
    // Future: OBJ_CLOSURE, OBJ_CLASS, OBJ_INSTANCE
} ObjectType;

// The base struct for all heap-allocated Angara objects.
// It MUST be the first member of any specific object struct.
typedef struct Object {
    ObjectType type;
    size_t ref_count; // For reference counting
} Object;

// The struct for an Angara string
typedef struct {
    Object obj;
    size_t length;
    char* chars; // Owns the character data
} AngaraString;

// Forward declaration for the List struct
typedef struct AngaraList AngaraList;

// --- The Universal Angara Value ---

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_I64,
    VAL_F64,
    VAL_OBJ // For any heap-allocated object
} AngaraValueType;

typedef struct {
    AngaraValueType type;
    union {
        bool boolean;
        int64_t i64;
        double f64;
        Object* obj;
    } as;
} AngaraObject;

// --- Concrete Data Structure Definitions ---

struct AngaraList {
    Object obj;
    size_t count;
    size_t capacity;
    AngaraObject* elements;
};

// A single key-value entry in a record
typedef struct {
    char* key; // Owns the key string
    AngaraObject value;
} RecordEntry;

typedef struct {
    Object obj;
    size_t count;
    size_t capacity;
    RecordEntry* entries;
} AngaraRecord;


// --- Helper Macros for Type Checking and Casting ---
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_I64(value)     ((value).type == VAL_I64)
#define IS_F64(value)     ((value).type == VAL_F64)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_I64(value)     ((value).as.i64)
// This macro smartly promotes integers to float when needed.
#define AS_F64(value)     (IS_F64(value) ? (value).as.f64 : (double)AS_I64(value))
#define AS_OBJ(value)     ((value).as.obj)

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)
#define AS_STRING(value)  ((AngaraString*)AS_OBJ(value))
#define AS_CSTRING(value) (AS_STRING(value)->chars)
#define AS_LIST(value)    ((AngaraList*)AS_OBJ(value))
#define AS_RECORD(value)  ((AngaraRecord*)AS_OBJ(value))

// --- Value Constructor Functions ---
AngaraObject create_nil(void);
AngaraObject create_bool(bool value);
AngaraObject create_i64(int64_t value);
AngaraObject create_f64(double value);
AngaraObject angara_string_from_c(const char* chars);

// --- Memory Management API ---
void angara_incref(AngaraObject value);
void angara_decref(AngaraObject value);

// --- Runtime Built-in Function API ---
void angara_print(int arg_count, AngaraObject args[]);
AngaraObject angara_len(AngaraObject collection);
bool angara_is_truthy(AngaraObject value);
AngaraObject angara_string_concat(AngaraObject a, AngaraObject b);
AngaraObject angara_string_equals(AngaraObject a, AngaraObject b);

// --- List API ---
AngaraObject angara_list_new(void);
AngaraObject angara_list_new_with_elements(size_t count, AngaraObject elements[]);
void angara_list_push(AngaraObject list, AngaraObject value);
AngaraObject angara_list_get(AngaraObject list, AngaraObject index);
void angara_list_set(AngaraObject list, AngaraObject index, AngaraObject value);

// --- Record API ---
AngaraObject angara_record_new(void);
AngaraObject angara_record_new_with_fields(size_t pair_count, AngaraObject kvs[]);
void angara_record_set(AngaraObject record, const char* key, AngaraObject value);
AngaraObject angara_record_get(AngaraObject record, const char* key);


#endif //ANGARA_RUNTIME_H
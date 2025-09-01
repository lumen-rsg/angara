//
// Created by cv2 on 9/1/25.
//

#ifndef ANGARA_RUNTIME_H
#define ANGARA_RUNTIME_H

#include <stdint.h> // For int64_t, etc.
#include <stdbool.h> // For bool
#include <stddef.h>  // For size_t

// --- Core Object Representation ---

// An enum to identify the type of heap-allocated objects
typedef enum {
    OBJ_STRING,
    OBJ_LIST,
    OBJ_RECORD
    // We will add OBJ_CLOSURE, OBJ_INSTANCE, etc. later
} ObjectType;

// The base struct for all heap-allocated Angara objects.
// It MUST be the first member of any specific object struct.
typedef struct Object {
    ObjectType type;
    size_t ref_count; // For our reference counting memory management
} Object;

// The struct for an Angara string
typedef struct {
    Object obj; // Base object header
    size_t length;
    char* chars;
} AngaraString;

// --- The Universal Angara Value ---

// An enum to identify the type of value stored in an AngaraObject
typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_I64,
    VAL_F64,
    VAL_OBJ // For any heap-allocated object (string, list, etc.)
} AngaraValueType;

// The main struct representing any Angara value
typedef struct {
    AngaraValueType type;
    union {
        bool boolean;
        int64_t i64;
        double f64;
        Object* obj; // Pointer to any heap-allocated object
    } as;
} AngaraObject;


// --- Forward declaration for the List struct ---
// This is needed because AngaraList contains AngaraObjects
typedef struct AngaraList AngaraList;

struct AngaraList {
    Object obj; // Base object header
    size_t count;
    size_t capacity;
    AngaraObject* elements;
};

// --- Helper Macros for Type Checking and Casting ---
#define IS_NIL(value)   ((value).type == VAL_NIL)
#define IS_BOOL(value)  ((value).type == VAL_BOOL)
#define IS_I64(value)   ((value).type == VAL_I64)
#define IS_F64(value)   ((value).type == VAL_F64)
#define IS_OBJ(value)   ((value).type == VAL_OBJ)

#define AS_BOOL(value)  ((value).as.boolean)
#define AS_I64(value)   ((value).as.i64)
#define AS_F64(value)   (IS_F64(value) ? (value).as.f64 : (double)AS_I64(value))
#define AS_OBJ(value)   ((value).as.obj)

#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define AS_STRING(value) ((AngaraString*)AS_OBJ(value))
#define AS_CSTRING(value) (((AngaraString*)AS_OBJ(value))->chars)
#define AS_LIST(value)   ((AngaraList*)AS_OBJ(value))

// --- Value Constructor Functions ---
AngaraObject create_nil();
AngaraObject create_bool(bool value);
AngaraObject create_i64(int64_t value);
AngaraObject create_f64(double value);
bool angara_is_truthy(AngaraObject value);

// --- Memory Management API ---
void angara_incref(AngaraObject value); // Increment reference count
void angara_decref(AngaraObject value); // Decrement, and free if it reaches 0

// --- Runtime Function API ---
void angara_print_object(AngaraObject value);
int64_t angara_len(AngaraObject value);

// String functions
AngaraObject angara_string_from_c(const char* chars);

// List functions
AngaraObject angara_list_new();
void angara_list_push(AngaraObject list, AngaraObject value);
AngaraObject angara_list_get(AngaraObject list, int64_t index);
void angara_list_set(AngaraObject list, int64_t index, AngaraObject value);


#endif //ANGARA_RUNTIME_H

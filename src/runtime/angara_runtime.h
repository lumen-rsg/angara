#ifndef ANGARA_RUNTIME_H
#define ANGARA_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h> // <-- Include for jmp_buf
#include <pthread.h>

// --- Core Object Representation ---

// An enum to identify the type of heap-allocated objects
typedef enum {
    OBJ_STRING,
    OBJ_LIST,
    OBJ_RECORD,
    OBJ_CLOSURE,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_THREAD,
    OBJ_MUTEX,
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
typedef struct AngaraClass AngaraClass;
typedef struct AngaraInstance AngaraInstance;

// The runtime representation of a class (the factory).
struct AngaraClass {
    Object obj;
    char* name;
};

// The base header for all class instances.
// The transpiler will create specific structs like `struct Angara_Point`
// that have this as their first member.
struct AngaraInstance {
    Object obj;
    AngaraClass* klass; // A pointer to the class it's an instance of.
};

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

typedef AngaraObject (*GenericAngaraFn)(int arg_count, AngaraObject args[]);
typedef struct {
    Object obj;
    GenericAngaraFn fn; // Pointer to the C function to call
    int arity;
    bool is_native; // Flag to distinguish
} AngaraClosure;


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

#define AS_CLASS(value)    ((AngaraClass*)AS_OBJ(value))
#define AS_INSTANCE(value) ((AngaraInstance*)AS_OBJ(value))
#define AS_CLOSURE(value) ((AngaraClosure*)AS_OBJ(value))
#define AS_THREAD(value) ((AngaraThread*)AS_OBJ(value))
#define AS_MUTEX(value) ((AngaraMutex*)AS_OBJ(value))

// --- Value Constructor Functions ---
AngaraObject angara_create_nil(void);
AngaraObject angara_create_bool(bool value);
AngaraObject angara_create_i64(int64_t value);
AngaraObject angara_create_f64(double value);
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
Object* angara_instance_new(size_t size, AngaraClass* klass);

// --- NEW: Exception Handling Globals and API ---
#define ANGARA_MAX_EXCEPTION_FRAMES 256

// The exception object that was most recently thrown.
extern AngaraObject g_current_exception;
// The stack of jump buffers for active `try` blocks.
extern jmp_buf g_exception_stack[ANGARA_MAX_EXCEPTION_FRAMES];
// The current depth of the exception stack.
extern int g_exception_stack_top;

// Pushes a new jump buffer and returns setjmp.
int angara_try_begin(void);
// Pops a jump buffer when a try block exits normally.
void angara_try_end(void);
// Throws an exception by calling longjmp.
void angara_throw(AngaraObject exception);

typedef struct ExceptionFrame {
    jmp_buf buffer;
    struct ExceptionFrame* prev; // A linked list
} ExceptionFrame;

extern ExceptionFrame* g_exception_chain_head;

void angara_runtime_init(void);
void angara_runtime_shutdown(void);

// --- NEW Thread struct ---
typedef struct {
    Object obj;
    pthread_t handle;
    AngaraObject return_value;
    AngaraObject closure_to_run;
} AngaraThread;


AngaraObject angara_spawn(AngaraObject closure);
AngaraObject angara_call(AngaraObject closure, int arg_count, AngaraObject args[]);

// We need a way to create closures.
AngaraObject angara_closure_new(GenericAngaraFn fn, int arity, bool is_native);
AngaraObject angara_thread_join(AngaraObject thread_obj);

typedef struct {
    Object obj;
    pthread_mutex_t handle;
} AngaraMutex;

// --- Mutex API functions ---
AngaraObject angara_mutex_new(void);
void angara_mutex_lock(AngaraObject mutex_obj);
void angara_mutex_unlock(AngaraObject mutex_obj);

AngaraObject angara_pre_increment(AngaraObject* lvalue);
AngaraObject angara_post_increment(AngaraObject* lvalue);
AngaraObject angara_pre_decrement(AngaraObject* lvalue);
AngaraObject angara_post_decrement(AngaraObject* lvalue);

AngaraObject angara_typeof(AngaraObject value);

void angara_throw_error(const char* message);
AngaraObject angara_create_string_no_copy(char* chars, size_t length);
AngaraObject angara_equals(AngaraObject a, AngaraObject b);

AngaraObject angara_to_string(AngaraObject value);
AngaraObject angara_to_i64(AngaraObject value);
AngaraObject angara_to_f64(AngaraObject value);
AngaraObject angara_to_bool(AngaraObject value);

#endif //ANGARA_RUNTIME_H
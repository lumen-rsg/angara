#ifndef ANGARA_H
#define ANGARA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <pthread.h>

/*
===========================================================================
 Part 1: Core Angara Data Structures
---------------------------------------------------------------------------
 This section defines the memory layout of all Angara values. It is
 essential for both the runtime and native module authors.
===========================================================================
*/

// --- The Universal Angara Value ---
typedef enum {
    VAL_NIL, VAL_BOOL, VAL_I64, VAL_F64, VAL_OBJ
} AngaraValueType;

typedef struct AngaraObject {
    AngaraValueType type;
    union {
        bool boolean;
        int64_t i64;
        double f64;
        struct Object* obj;
    } as;
} AngaraObject;


// --- Heap-Allocated Objects ---
typedef enum {
    OBJ_STRING, OBJ_LIST, OBJ_RECORD, OBJ_EXCEPTION, OBJ_THREAD, OBJ_MUTEX,
    OBJ_CLOSURE, OBJ_CLASS, OBJ_INSTANCE, OBJ_NATIVE_INSTANCE
} ObjectType;

typedef struct Object {
    ObjectType type;
    size_t ref_count;
} Object;


// --- Concrete Object Struct Definitions ---
// These are the specific layouts for all object types. While module authors
// primarily interact with them via `AngaraObject` and helper macros, these
// definitions are required for the macros to work correctly.

typedef struct {
    Object obj;
    size_t length;
    char* chars;
} AngaraString;

typedef struct AngaraList {
    Object obj;
    size_t count;
    size_t capacity;
    AngaraObject* elements;
} AngaraList;

typedef struct {
    char* key;
    AngaraObject value;
} RecordEntry;

typedef struct AngaraRecord {
    Object obj;
    size_t count;
    size_t capacity;
    RecordEntry* entries;
} AngaraRecord;

typedef struct {
    Object obj;
    AngaraObject message;
} AngaraException;

typedef struct AngaraClass {
    Object obj;
    char* name;
} AngaraClass;

typedef struct AngaraInstance {
    Object obj;
    AngaraClass* klass;
} AngaraInstance;

typedef AngaraObject (*GenericAngaraFn)(int arg_count, AngaraObject args[]);
typedef struct AngaraClosure {
    Object obj;
    GenericAngaraFn fn;
    int arity;
    bool is_native;
} AngaraClosure;

typedef struct AngaraThread {
    Object obj;
    pthread_t handle;
    AngaraObject return_value;
    AngaraObject closure_to_run;
} AngaraThread;

typedef struct AngaraMutex {
    Object obj;
    pthread_mutex_t handle;
} AngaraMutex;

typedef void (*AngaraFinalizerFn)(void* data);
typedef struct {
    Object obj;
    void* data;
    AngaraFinalizerFn finalizer;
} AngaraNativeInstance;


/*
===========================================================================
 Part 2: Host API - Functions Provided by the Angara Runtime
---------------------------------------------------------------------------
 These are the functions that a native module can call to interact with
 the language, create values, and manage memory.
===========================================================================
*/

// --- Value Constructors ---
AngaraObject angara_create_nil(void);
AngaraObject angara_create_bool(bool value);
AngaraObject angara_create_i64(int64_t value);
AngaraObject angara_create_f64(double value);
AngaraObject angara_create_string(const char* chars);
AngaraObject angara_create_string_no_copy(char* owned_chars, size_t length);
AngaraObject angara_create_native_instance(void* data, AngaraFinalizerFn finalizer);
AngaraObject angara_exception_new(AngaraObject message);
AngaraObject angara_list_new(void);
AngaraObject angara_record_new(void);
AngaraObject angara_mutex_new(void);
AngaraObject angara_closure_new(GenericAngaraFn fn, int arity, bool is_native);
AngaraObject angara_string_from_c(const char* chars);
AngaraObject angara_to_string(AngaraObject value);
AngaraObject angara_string_concat(AngaraObject a, AngaraObject b);
AngaraObject angara_record_get_with_angara_key(AngaraObject record_obj, AngaraObject key_obj);
void angara_record_set_with_angara_key(AngaraObject record_obj, AngaraObject key_obj, AngaraObject value_obj);
AngaraObject angara_record_get(AngaraObject record_obj, const char* key);
void angara_record_set(AngaraObject record_obj, const char* key, AngaraObject value);
AngaraObject angara_record_new_with_fields(size_t pair_count, AngaraObject kvs[]);

// --- Error Handling & Debugging ---
void angara_throw_error(const char* message);
void angara_debug_print(const char* message);

// --- Memory Management ---
void angara_incref(AngaraObject value);
void angara_decref(AngaraObject value);

// --- List Manipulation ---
void angara_list_push(AngaraObject list, AngaraObject value);

// --- Runtime Internals (for generated code) ---
// These are used by the transpiler but not typically by module authors directly.
void angara_throw(AngaraObject exception);
AngaraObject angara_call(AngaraObject closure, int arg_count, AngaraObject args[]);


/*
===========================================================================
 Part 3: Native Module ABI - The Contract for C Modules
---------------------------------------------------------------------------
 This section defines the structures and conventions that a C source file
 must follow to be recognizable as a valid Angara native module.
===========================================================================
*/

// --- C Function Signatures ---
typedef AngaraObject (*AngaraGlobalFn)(int arg_count, AngaraObject* args);
typedef AngaraObject (*AngaraMethodFn)(int arg_count, AngaraObject* args); // `self` will be args[0]
typedef AngaraObject (*AngaraCtorFn)(int arg_count, AngaraObject* args);
typedef enum { DEF_FUNCTION, DEF_CLASS } AngaraDefType;

// --- ABI Definition Structs ---
// Forward-declare AngaraClassDef so AngaraFuncDef can point to it.
struct AngaraClassDef;
typedef AngaraObject (*AngaraNativeFn)(int arg_count, AngaraObject* args);

// For exporting a global function OR a constructor.
typedef struct AngaraFuncDef {
    // The name of the function as it appears in Angara (e.g., "read_file" or "Counter")
    const char* name;
    // A pointer to the C implementation of the function.
    AngaraGlobalFn function;
    // A string describing the function's Angara signature (e.g., "s->i")
    const char* type_string;
    // If this function is a constructor, this points to the definition of the class it constructs.
    // If this is a regular global function, this MUST be NULL.
    const struct AngaraClassDef* constructs;
} AngaraFuncDef;

// For defining a method on a native class.
typedef struct {
    const char* name;
    AngaraMethodFn function; // Pointer to the method's C implementation
    const char* type_string; // Signature of the method's parameters and return
} AngaraMethodDef;

// For defining a field on a native class.
typedef struct {
    const char* name;
    const char* type_string; // The type of the field (e.g., "i" for i64)
    bool is_const;
} AngaraFieldDef;

// For defining the structure of a native class.
typedef struct AngaraClassDef {
    const char* name; // The name of the class (e.g., "Counter")
    const AngaraFieldDef* fields;   // A null-terminated array of fields
    const AngaraMethodDef* methods; // A null-terminated array of methods
} AngaraClassDef;

// --- THIS IS THE CRUCIAL PART ---
// The top-level definition is a tagged union.
typedef struct {
    AngaraDefType type;
    union {
        const AngaraFuncDef* function;
        const AngaraClassDef* class_def;
    } as;
} AngaraModuleDef;

// --- Helper Macros for ABI Definitions ---
#define ANGARA_FUNCTION(func_def_ptr) { .type = DEF_FUNCTION, .as = { .function = (func_def_ptr) } }
#define ANGARA_CLASS(class_def_ptr)   { .type = DEF_CLASS, .as = { .class_def = (class_def_ptr) } }
#define ANGARA_SENTINEL               { .type = 0 }

// --- Module Entry Point Macro ---
#define ANGARA_ABI_PASTE_IMPL(a, b) a##b
#define ANGARA_ABI_PASTE(a, b) ANGARA_ABI_PASTE_IMPL(a, b)
#define ANGARA_MODULE_INIT(MODULE_NAME) \
    const AngaraFuncDef* ANGARA_ABI_PASTE(Angara_, ANGARA_ABI_PASTE(MODULE_NAME, _Init))(int* def_count)


/*
===========================================================================
 Part 4: Helper Macros for C Code
---------------------------------------------------------------------------
 These macros are for use inside C functions (runtime or modules) to
 safely inspect and manipulate AngaraObjects.
===========================================================================
*/
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_I64(value)     ((value).type == VAL_I64)
#define IS_F64(value)     ((value).type == VAL_F64)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_I64(value)     ((value).as.i64)
#define AS_F64(value)     ((value).as.f64)
#define AS_OBJ(value)     ((value).as.obj)

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

#define IS_STRING(value)  (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_STRING)
#define AS_STRING(value)  ((AngaraString*)AS_OBJ(value))
#define AS_CSTRING(value) (AS_STRING(value)->chars)

#define IS_LIST(value)    (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_LIST)
#define AS_LIST(value)    ((AngaraList*)AS_OBJ(value))

#define IS_RECORD(value)  (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_RECORD)
#define AS_RECORD(value)  ((AngaraRecord*)AS_OBJ(value))

#define IS_EXCEPTION(value) (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_EXCEPTION)
#define AS_EXCEPTION(value) ((AngaraException*)AS_OBJ(value))

#define IS_NATIVE_INSTANCE(value) (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_NATIVE_INSTANCE)
#define AS_NATIVE_INSTANCE(value) ((AngaraNativeInstance*)AS_OBJ(value))

#define AS_CLASS(value)    ((AngaraClass*)AS_OBJ(value))
#define AS_INSTANCE(value) ((AngaraInstance*)AS_OBJ(value))
#define AS_CLOSURE(value)  ((AngaraClosure*)AS_OBJ(value))
#define AS_THREAD(value)   ((AngaraThread*)AS_OBJ(value))
#define AS_MUTEX(value)    ((AngaraMutex*)AS_OBJ(value))


/*
===========================================================================
 Part 5: Exception Handling Internals (for generated code)
---------------------------------------------------------------------------
 These definitions are required by the C code generated by the transpiler.
===========================================================================
*/
#define ANGARA_MAX_EXCEPTION_FRAMES 256
extern AngaraObject g_current_exception;
typedef struct ExceptionFrame { jmp_buf buffer; struct ExceptionFrame* prev; } ExceptionFrame;
extern ExceptionFrame* g_exception_chain_head;

extern void angara_runtime_init();
extern void angara_runtime_shutdown();
extern bool angara_is_truthy(AngaraObject value);
AngaraObject angara_equals(AngaraObject a, AngaraObject b);
Object* angara_instance_new(size_t size, AngaraClass* klass);
AngaraObject angara_len(AngaraObject collection);
AngaraObject angara_to_f64(AngaraObject value);
AngaraObject angara_list_get(AngaraObject list_obj, AngaraObject index_obj);
void angara_list_set(AngaraObject list_obj, AngaraObject index_obj, AngaraObject value);
AngaraObject angara_to_i64(AngaraObject value);
AngaraObject angara_to_string(AngaraObject value);
AngaraObject angara_typeof(AngaraObject value);
AngaraObject angara_create_string(const char* chars);
#endif // ANGARA_H
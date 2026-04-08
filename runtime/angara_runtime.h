#ifndef ANGARA_H
#define ANGARA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <pthread.h>

/* ==========================================================================
 * ANGARA RUNTIME — PUBLIC API
 * ==========================================================================
 *
 * This section is the stable, documented interface for native module authors.
 * If you are writing a C module for Angara, this is your API reference.
 *
 * Quick Start for Module Authors:
 * -------------------------------
 * 1. Include this header in your .c file
 * 2. Use angara_create_*() to build return values
 * 3. Use angara_as_c_*() macros to extract C values from arguments
 * 4. Use IS_* and AS_* macros to inspect and unwrap arguments
 * 5. Use angara_throw_error() to report runtime errors
 * 6. Export your module using ANGARA_FUNCTION / ANGARA_CLASS + ANGARA_SENTINEL
 * 7. Define the entry point with ANGARA_MODULE_INIT(YourModuleName)
 * ========================================================================== */

/* Forward declarations for data/enum vtable types (fully defined in §P3). */
typedef struct AngaraDataInfo AngaraDataInfo;
typedef struct AngaraEnumInfo AngaraEnumInfo;

/*
  §12  Runtime Type Checks (inline)
============================================================================
  §1  Core Value System
────────────────────────────────────────────────────────────────────────────

  Every value in Angara is an AngaraObject — a tagged union that fits in
  24 bytes (type tag + 8-byte payload).  Values come in two flavours:

    • Inline values   — nil, bool, i64, f64   (stored directly, no allocation)
    • Heap objects    — string, list, record…  (reference-counted allocation)

  The `type` field (AngaraValueType) tells you which union member is active.
  For heap objects, `OBJ_TYPE(value)` drills into the Object header to get
  the specific ObjectType.
============================================================================
*/

/// The tag that discriminates which payload is stored in an AngaraObject.
typedef enum {
    VAL_NIL,    ///< The nil / void / unit value
    VAL_BOOL,   ///< A boolean  (access via AS_BOOL)
    VAL_I64,    ///< A 64-bit signed integer  (access via AS_I64)
    VAL_F64,    ///< A 64-bit IEEE 754 float  (access via AS_F64)
    VAL_OBJ     ///< A pointer to a heap-allocated Object  (access via AS_OBJ)
} AngaraValueType;

/// The universal value type — every Angara value is represented by this struct.
typedef struct AngaraObject {
    AngaraValueType type;
    union {
        bool           boolean;  ///< Valid when type == VAL_BOOL
        int64_t        i64;      ///< Valid when type == VAL_I64
        double         f64;      ///< Valid when type == VAL_F64
        struct Object* obj;      ///< Valid when type == VAL_OBJ
    } as;
} AngaraObject;


/*
============================================================================
  §2  Heap Object Types
────────────────────────────────────────────────────────────────────────────

  All heap-allocated values share a common Object header:
    ┌─────────────┬────────────┐
    │ ObjectType   │ ref_count  │
    │  (4 bytes)   │ (8 bytes)  │
    └─────────────┴────────────┘

  The ObjectType enum identifies the concrete layout that follows the header.
  Module authors typically interact with objects through the IS_ /AS_ macros
  rather than reading these fields directly.
============================================================================
*/

/// Discriminator for the concrete type of a heap-allocated Object.
typedef enum {
    OBJ_STRING,          ///< AngaraString    — immutable UTF-8 string
    OBJ_LIST,            ///< AngaraList      — growable array of AngaraObject
    OBJ_RECORD,          ///< AngaraRecord    — dynamic key-value map
    OBJ_EXCEPTION,       ///< AngaraException — error object with a message
    OBJ_THREAD,          ///< AngaraThread    — managed pthread wrapper
    OBJ_MUTEX,           ///< AngaraMutex     — managed pthread_mutex wrapper
    OBJ_CLOSURE,         ///< AngaraClosure   — first-class function
    OBJ_CLASS,           ///< AngaraClass     — class metadata
    OBJ_INSTANCE,        ///< AngaraInstance  — class instance
    OBJ_NATIVE_INSTANCE, ///< AngaraNativeInstance — opaque C data wrapper
    OBJ_DATA_INSTANCE,   ///< AngaraDataInstanceHeader — data class instance
    OBJ_ENUM_INSTANCE,   ///< AngaraEnumInstanceHeader — enum variant instance
    OBJ_BOUND_METHOD     ///< AngaraBoundMethod — receiver + method closure
} ObjectType;

/// Common header for ALL heap-allocated objects. Every concrete object type
/// starts with this as its first member (inheritance-by-embedding in C).
typedef struct Object {
    ObjectType type;
    size_t ref_count;   ///< ARC reference count; managed by angara_incref/decref
} Object;

/// Function signature for all Angara closures (native and transpiled).
typedef AngaraObject (*GenericAngaraFn)(int arg_count, AngaraObject args[]);

/// Finalizer callback for AngaraNativeInstance — called when ref_count hits 0.
typedef void (*AngaraFinalizerFn)(void* data);

/*
============================================================================
  §3  Concrete Object Layouts
────────────────────────────────────────────────────────────────────────────

  Each struct below embeds `Object` as its first member so it can be safely
  cast to/from Object*.  Module authors should access fields through the
  AS_* macros defined in §5.
============================================================================
*/

/// Immutable, heap-allocated UTF-8 string.
typedef struct {
    Object obj;
    size_t length;   ///< Number of bytes (not including NUL terminator)
    char*  chars;    ///< NUL-terminated UTF-8 data
} AngaraString;

/// Growable, heap-allocated array of AngaraObject values.
typedef struct AngaraList {
    Object obj;
    size_t count;     ///< Number of active elements
    size_t capacity;  ///< Allocated capacity
    AngaraObject* elements;  ///< Element storage
} AngaraList;

/// Single entry in a Record's key-value store.
typedef struct {
    char*          key;    ///< NUL-terminated C string key (owned by the record)
    AngaraObject   value;  ///< The associated value
} RecordEntry;

/// Dynamic key-value map (Angara's `record` type).
typedef struct AngaraRecord {
    Object obj;
    size_t count;     ///< Number of active entries
    size_t capacity;  ///< Allocated capacity
    RecordEntry* entries;
} AngaraRecord;

/// Exception object carrying a human-readable message string.
typedef struct {
    Object obj;
    AngaraObject message;  ///< Always an OBJ_STRING
} AngaraException;

/// Metadata for a class declaration (name only; fields/methods are runtime-managed).
typedef struct AngaraClass {
    Object obj;
    char*  name;  ///< Class name (e.g., "Counter")
} AngaraClass;

/// An instance of an AngaraClass.  Fields are stored after this header in memory.
typedef struct AngaraInstance {
    Object obj;
    AngaraClass* klass;  ///< Pointer to the class this is an instance of
} AngaraInstance;

/// A first-class function closure.
typedef struct AngaraClosure {
    Object obj;
    GenericAngaraFn fn;       ///< The C function implementing this closure
    int   arity;              ///< Expected argument count (-1 = variadic)
    bool  is_native;          ///< true if fn is a native module function
} AngaraClosure;

/// Opaque wrapper around a C pointer, with an optional finalizer.
/// Used by native modules to wrap external resources.
typedef struct {
    Object obj;
    void*             data;       ///< The opaque C pointer
    AngaraFinalizerFn finalizer;  ///< Called on dealloc, or NULL
    const char*       type_name;  ///< Human-readable type name for debugging
} AngaraNativeInstance;


/*
============================================================================
  §4  Type Inspection & Unwrapping Macros
────────────────────────────────────────────────────────────────────────────

  Quick type checks:    IS_NIL(v), IS_BOOL(v), IS_I64(v), IS_F64(v), IS_OBJ(v)
  Unwrap values:        AS_BOOL(v), AS_I64(v), AS_F64(v), AS_OBJ(v)
  Drill into objects:   OBJ_TYPE(v)  → ObjectType
  Typed checks/casts:   IS_STRING(v)/AS_STRING(v), IS_LIST(v)/AS_LIST(v), …

  All IS_* macros return bool.  All AS_* macros return a pointer to the
  concrete struct.  Always guard AS_* with the corresponding IS_* check.
============================================================================
*/

/* ── Value-level checks ──────────────────────────────── */
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_I64(value)     ((value).type == VAL_I64)
#define IS_F64(value)     ((value).type == VAL_F64)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

/* ── Value unwrapping ────────────────────────────────── */
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_I64(value)     ((value).as.i64)
#define AS_F64(value)     ((value).as.f64)
#define AS_OBJ(value)     ((value).as.obj)

/* ── Object-level type tag ───────────────────────────── */
#define OBJ_TYPE(value)   (AS_OBJ(value)->type)

/* ── Typed object checks and casts ───────────────────── */
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

/// Box a raw Object pointer into an AngaraObject value.
#define BOX_PTR(ptr) ((AngaraObject){VAL_OBJ, {.obj = (Object*)(ptr)}})


/*
============================================================================
  §5  Value Constructors
────────────────────────────────────────────────────────────────────────────

  These functions allocate and return a new AngaraObject.  For inline types
  (nil, bool, i64, f64) the result is stack-allocated and needs no cleanup.
  For object types (string, list, record, …) the result is heap-allocated
  with an initial ref_count of 1.

  Ownership rule: the caller owns the returned reference.  Transfer it to
  Angara by returning it from your function, or call angara_decref() when done.
============================================================================
*/

/// Create the nil value.
AngaraObject angara_create_nil(void);

/// Create a boolean value.
AngaraObject angara_create_bool(bool value);

/// Create a 64-bit signed integer value.
AngaraObject angara_create_i64(int64_t value);

/// Create a 64-bit floating-point value.
AngaraObject angara_create_f64(double value);

/// Create a string by copying the given C string.
/// @param chars  NUL-terminated UTF-8 C string.  The data is copied; caller retains ownership.
/// @return       A new OBJ_STRING value.
AngaraObject angara_create_string(const char* chars);

/// Create a string from a buffer with explicit length.
/// @param chars   Pointer to the character data (need not be NUL-terminated).
/// @param length  Number of bytes to copy.
/// @return        A new OBJ_STRING value.
AngaraObject angara_create_string_with_len(const char* chars, size_t length);

/// Create a string by taking ownership of a pre-allocated buffer.
/// @param owned_chars  A heap-allocated, NUL-terminated buffer.  Ownership transfers to the string.
/// @param length       Number of bytes in owned_chars (excluding NUL).
/// @return             A new OBJ_STRING value.
AngaraObject angara_create_string_no_copy(char* owned_chars, size_t length);

/// Alias for angara_create_string() — creates a string from a C string.
AngaraObject angara_string_from_c(const char* chars);

/// Create a new empty list.
AngaraObject angara_list_new(void);

/// Create a new list pre-populated with the given elements.
/// @param count     Number of elements.
/// @param elements  Array of AngaraObject values to populate the list with.
AngaraObject angara_list_new_with_elements(size_t count, AngaraObject elements[]);

/// Create a new empty record.
AngaraObject angara_record_new(void);

/// Create a record pre-populated with key-value pairs.
/// @param pair_count  Number of key-value pairs.
/// @param kvs         Flat array: kvs[0]=key0, kvs[1]=val0, kvs[2]=key1, …
/// @return            A new OBJ_RECORD value.
AngaraObject angara_record_new_with_fields(size_t pair_count, AngaraObject kvs[]);

/// Create a new exception object with the given message string.
/// @param message  An OBJ_STRING value containing the error message.
AngaraObject angara_exception_new(AngaraObject message);

/// Create a closure wrapping a C function pointer.
/// @param fn         The C function implementing the closure.
/// @param arity      Expected argument count, or -1 for variadic.
/// @param is_native  true if this is a native module function.
AngaraObject angara_closure_new(GenericAngaraFn fn, int arity, bool is_native);

/// Create a new opaque native instance wrapping a C pointer.
/// @param data       The opaque C pointer to wrap.
/// @param finalizer  Called with `data` when the instance is deallocated, or NULL.
/// @param type_name  A human-readable name for debugging (e.g., "FileHandle").
AngaraObject angara_create_native_instance(void* data, AngaraFinalizerFn finalizer, const char* type_name);


/*
============================================================================
  §6  Memory Management (ARC)
────────────────────────────────────────────────────────────────────────────

  Angara uses automatic reference counting (ARC).  Every heap object carries
  a ref_count.  When it drops to 0, the object is deallocated.

  The transpiler automatically inserts incref/decref calls.  Module authors
  should only need these when manually storing or releasing AngaraObject values
  outside of what Angara manages.
============================================================================
*/

/// Increment the reference count of value (if it is a heap object).
void angara_incref(AngaraObject value);

/// Decrement the reference count of value (if it is a heap object).
/// If ref_count reaches 0, the object is deallocated.
void angara_decref(AngaraObject value);


/*
============================================================================
  §7  String Operations
────────────────────────────────────────────────────────────────────────────
*/

/// Concatenate two Angara strings, returning a new string.
/// @param a  The left-hand string (OBJ_STRING).
/// @param b  The right-hand string (OBJ_STRING).
/// @return   A new OBJ_STRING containing a+b.
AngaraObject angara_string_concat(AngaraObject a, AngaraObject b);


/*
============================================================================
  §8  Collection Operations
────────────────────────────────────────────────────────────────────────────
*/

/* ── List Operations ─────────────────────────────────── */

/// Append a value to the end of a list.
/// @param list   An OBJ_LIST value.
/// @param value  The value to append.  Angara increments its ref_count.
void angara_list_push(AngaraObject list, AngaraObject value);

/// Retrieve an element from a list by index.
/// @param list       An OBJ_LIST value.
/// @param index_obj  An VAL_I64 value (0-based index).
/// @return           The element at the given index, or nil if out of bounds.
AngaraObject angara_list_get(AngaraObject list_obj, AngaraObject index_obj);

/// Set an element in a list by index.
/// @param list_obj    An OBJ_LIST value.
/// @param index_obj   An VAL_I64 value (0-based index).
/// @param value       The new value to store.
void angara_list_set(AngaraObject list_obj, AngaraObject index_obj, AngaraObject value);

/// Remove the element at the given index, returning it.
/// @return  The removed element, or nil if index is out of bounds.
AngaraObject angara_list_remove_at(AngaraObject list, AngaraObject index);

/// Remove the first occurrence of value from the list.
/// @return  true if the value was found and removed, false otherwise.
AngaraObject angara_list_remove(AngaraObject list, AngaraObject value);

/// Return the number of elements in a list or characters in a string.
AngaraObject angara_len(AngaraObject collection);

/* ── Record Operations ───────────────────────────────── */

/// Get a value from a record by C string key.
/// @param record_obj  An OBJ_RECORD value.
/// @param key         NUL-terminated C string key.
/// @return            The value associated with key, or nil if not found.
AngaraObject angara_record_get(AngaraObject record_obj, const char* key);

/// Set a value in a record by C string key.  If the key already exists, its
/// value is replaced.  If not, a new entry is added.
/// @param record_obj  An OBJ_RECORD value.
/// @param key         NUL-terminated C string key.
/// @param value       The value to associate with the key.
void angara_record_set(AngaraObject record_obj, const char* key, AngaraObject value);

/// Get a value from a record by Angara string key.
/// @param key_obj  An OBJ_STRING value used as the key.
/// @return         The associated value, or nil if not found.
AngaraObject angara_record_get_with_angara_key(AngaraObject record_obj, AngaraObject key_obj);

/// Set a value in a record by Angara string key.
/// @param key_obj    An OBJ_STRING value used as the key.
/// @param value_obj  The value to associate.
void angara_record_set_with_angara_key(AngaraObject record_obj, AngaraObject key_obj, AngaraObject value_obj);

/// Remove an entry from a record by Angara string key.
/// @return  true if the key was found and removed, false otherwise.
AngaraObject angara_record_remove(AngaraObject record, AngaraObject key);

/// Return a list of all keys in the record (as OBJ_STRING values).
AngaraObject angara_record_keys(AngaraObject record);

/// Create a shallow clone of a record.
AngaraObject angara_record_clone(AngaraObject record);

/// Get a value from either a list (by integer index) or a record (by string key).
AngaraObject angara_get(AngaraObject container, AngaraObject key);


/*
============================================================================
  §9  Type Conversions & Inspection
────────────────────────────────────────────────────────────────────────────
*/

/// Convert any Angara value to an i64.  Booleans map to 0/1, floats are
/// truncated, strings are parsed with strtoll, nil becomes 0.
AngaraObject angara_to_i64(AngaraObject value);

/// Convert any Angara value to an f64.  Booleans map to 0.0/1.0, integers
/// are widened, strings are parsed with strtod, nil becomes 0.0.
AngaraObject angara_to_f64(AngaraObject value);

/// Convert any Angara value to a boolean.  Uses Angara's truthiness rules:
/// nil → false, 0 → false, 0.0 → false, "" → false, empty list → false,
/// everything else → true.
AngaraObject angara_to_bool(AngaraObject value);

/// Convert any Angara value to its string representation.
/// For strings, returns the same value (with an incref).  For other types,
/// produces a human-readable representation.
AngaraObject angara_to_string(AngaraObject value);

/// Return the type name of a value as a string (e.g., "nil", "bool", "string", "list").
AngaraObject angara_typeof(AngaraObject value);

/// Deep structural equality comparison.  Strings compare by content, lists
/// and records compare element-wise.  Different types are never equal,
/// except i64/f64 cross-comparison.
AngaraObject angara_equals(AngaraObject a, AngaraObject b);

/// Check if a value is an instance of the named type.
/// @param object      The value to check.
/// @param class_name  The Angara type name (e.g., "string", "MyClass").
/// @return            A boolean AngaraObject.
AngaraObject angara_is_instance_of(AngaraObject object, const char* class_name);

/// Check if all elements of a list are instances of the named type.
/// Returns true for empty lists.
AngaraObject angara_is_list_of_type(AngaraObject list, const char* element_type_name);

/// Create a deep, recursive clone of the given value.  Strings are shared
/// (immutable).  Lists and records are recursively cloned.  Reference types
/// (closures, threads, mutexes) are shallow-copied.
AngaraObject angara_deep_clone(AngaraObject value);

/// Test whether an Angara value is truthy (non-nil, non-zero, non-empty).
extern bool angara_is_truthy(AngaraObject value);


/*
============================================================================
  §10  Error Handling
────────────────────────────────────────────────────────────────────────────

  Module authors should use angara_throw_error() to signal runtime failures.
  This creates an Angara exception and unwinds the call stack to the nearest
  try/catch block (or terminates the program if unhandled).
============================================================================
*/

/// Throw a runtime error with a human-readable C string message.
/// This function does not return — it longjmps to the nearest catch handler.
/// @param message  A NUL-terminated C string describing the error.
void angara_throw_error(const char* message);

/// Print a debug message to stderr (for development purposes).
void angara_debug_print(const char* message);


/*
============================================================================
  §11  FFI Boxing & Unboxing
────────────────────────────────────────────────────────────────────────────

  These macros and functions convert between raw C types and AngaraObject.

  Unboxing (Angara → C):  Use the angara_as_c_* macros.
  Boxing    (C → Angara):  Use the angara_from_c_* functions.

  All angara_from_c_* functions return a newly-owned AngaraObject.
  The angara_as_c_* macros are zero-cost — they simply cast the union member.
============================================================================
*/

/* ── Unboxing: AngaraObject → raw C type (zero-cost macros) ── */
#define angara_as_c_i64(obj)    (AS_I64(obj))
#define angara_as_c_i32(obj)    ((int32_t)AS_I64(obj))
#define angara_as_c_i16(obj)    ((int16_t)AS_I64(obj))
#define angara_as_c_i8(obj)     ((int8_t)AS_I64(obj))
#define angara_as_c_c_ptr(obj)  ((void*)AS_I64(obj))

#define angara_as_c_u64(obj)    ((uint64_t)AS_I64(obj))
#define angara_as_c_u32(obj)    ((uint32_t)AS_I64(obj))
#define angara_as_c_u16(obj)    ((uint16_t)AS_I64(obj))
#define angara_as_c_u8(obj)     ((uint8_t)AS_I64(obj))

#define angara_as_c_f64(obj)    (AS_F64(obj))
#define angara_as_c_f32(obj)    ((float)AS_F64(obj))

#define angara_as_c_bool(obj)   (AS_BOOL(obj))

/* ── Boxing: raw C type → AngaraObject (functions) ── */

/// Box an int32_t into an Angara i64 value.
AngaraObject angara_from_c_i32(int32_t value);

/// Box a uint32_t into an Angara i64 value.
AngaraObject angara_from_c_u32(uint32_t value);

/// Box an int64_t into an Angara i64 value.
AngaraObject angara_from_c_i64(int64_t value);

/// Box a uint64_t into an Angara i64 value.
AngaraObject angara_from_c_u64(uint64_t value);

/// Box a double into an Angara f64 value.
AngaraObject angara_from_c_f64(double value);

/// Box a bool into an Angara bool value.
AngaraObject angara_from_c_bool(bool value);

/// Box a NUL-terminated C string into an Angara string value.
AngaraObject angara_from_c_string(const char* value);

/// Box a void pointer into an Angara i64 value (stored as an integer address).
AngaraObject angara_from_c_c_ptr(void* value);

/// Box a raw Object pointer into an AngaraObject value.
AngaraObject angara_from_c_object(void* ptr);


/*
============================================================================
  §12  Runtime Type Checks (inline)
────────────────────────────────────────────────────────────────────────────

  Fast type checks that avoid the overhead of angara_is_instance_of() when
  you already have access to the class/data vtable pointer.
============================================================================
*/

typedef struct {
    Object obj;
    AngaraDataInfo* info;
} AngaraDataInstanceHeader;

typedef struct {
    Object obj;
    AngaraEnumInfo* info;
} AngaraEnumInstanceHeader;

/// Check if a raw pointer is an AngaraInstance of the given AngaraClass.
static inline bool angara_is_class(void* obj, AngaraClass* target) {
    if (!obj) return false;
    Object* o = (Object*)obj;
    return o->type == OBJ_INSTANCE && ((AngaraInstance*)o)->klass == target;
}

/// Check if an AngaraObject is an instance of the given AngaraClass.
static inline bool angara_is_class_obj(AngaraObject wrapper, AngaraClass* target) {
    if (!IS_OBJ(wrapper)) return false;
    AngaraInstance* instance = (AngaraInstance*)wrapper.as.obj;
    return instance->klass == target;
}

/// Check if a raw pointer is a data instance matching the given AngaraDataInfo vtable.
static inline bool angara_is_data(void* obj, AngaraDataInfo* target) {
    if (!obj) return false;
    Object* o = (Object*)obj;
    return o->type == OBJ_DATA_INSTANCE && ((AngaraDataInstanceHeader*)o)->info == target;
}


/* ==========================================================================
 * §13  NATIVE MODULE ABI
 * ==========================================================================
 *
 * This section defines the contract that a C source file must follow to be
 * recognized as a valid Angara native module.
 *
 * A native module exports:
 *   1. A set of AngaraFuncDef and/or AngaraClassDef descriptors
 *   2. An entry point function named via ANGARA_MODULE_INIT(ModuleName)
 *
 * Example - a module exporting one global function:
 *
 *   static AngaraObject hello(int argc, AngaraObject* args) {
 *       return angara_string_from_c("Hello from C!");
 *   }
 *
 *   static const AngaraFuncDef hello_def = {
 *       .name = "hello",
 *       .function = hello,
 *       .type_string = "->s",
 *       .constructs = NULL
 *   };
 *
 *   ANGARA_MODULE_INIT(MyModule) {
 *       *def_count = 1;
 *       return &hello_def;
 *   }
 *
 * Example - a module exporting a class:
 *   // ...define methods, fields, constructor...
 *   // ...use ANGARA_CLASS() in the module init...
 * ========================================================================== */

/// Function signature for a global Angara function implemented in C.
typedef AngaraObject (*AngaraGlobalFn)(int arg_count, AngaraObject* args);

/// Function signature for a method on a native class.
/// `self` is always args[0].
typedef AngaraObject (*AngaraMethodFn)(int arg_count, AngaraObject* args);

/// Function signature for a constructor of a native class.
typedef AngaraObject (*AngaraCtorFn)(int arg_count, AngaraObject* args);

/// Generic native function pointer (used internally).
typedef AngaraObject (*AngaraNativeFn)(int arg_count, AngaraObject* args);

/// Tag for the top-level AngaraModuleDef union.
typedef enum { DEF_FUNCTION, DEF_CLASS } AngaraDefType;

/// Describes an exported global function or constructor.
typedef struct AngaraFuncDef {
    const char*         name;         ///< Name as seen in Angara (e.g., "read_file")
    AngaraGlobalFn      function;     ///< C implementation
    const char*         type_string;  ///< Type signature (e.g., "s->i" means string→i64)
    const struct AngaraClassDef* constructs;  ///< Non-NULL if this is a constructor
} AngaraFuncDef;

/// Describes a method on a native class.
typedef struct {
    const char*     name;         ///< Method name as seen in Angara
    AngaraMethodFn  function;     ///< C implementation (self = args[0])
    const char*     type_string;  ///< Type signature
} AngaraMethodDef;

/// Describes a field on a native class.
typedef struct {
    const char* name;          ///< Field name
    const char* type_string;   ///< Field type code (e.g., "i" for i64, "s" for string)
    bool        is_const;      ///< If true, the field is read-only
} AngaraFieldDef;

/// Describes a native class with fields and methods.
typedef struct AngaraClassDef {
    const char*            name;     ///< Class name (e.g., "Counter")
    const AngaraFieldDef*  fields;   ///< NUL-terminated array of field descriptors
    const AngaraMethodDef* methods;  ///< NUL-terminated array of method descriptors
} AngaraClassDef;

/// Top-level tagged union — each entry in the module's export table is one of these.
typedef struct {
    AngaraDefType type;
    union {
        const AngaraFuncDef*  function;   ///< Valid when type == DEF_FUNCTION
        const AngaraClassDef* class_def;  ///< Valid when type == DEF_CLASS
    } as;
} AngaraModuleDef;

/* ── ABI Helper Macros ───────────────────────────────── */

/// Wrap a pointer to AngaraFuncDef as a module export entry.
#define ANGARA_FUNCTION(func_def_ptr) { .type = DEF_FUNCTION, .as = { .function = (func_def_ptr) } }

/// Wrap a pointer to AngaraClassDef as a module export entry.
#define ANGARA_CLASS(class_def_ptr) { .type = DEF_CLASS, .as = { .class_def = (class_def_ptr) } }

/// Sentinel value marking the end of a module's export table.
#define ANGARA_SENTINEL { .type = 0 }

/* ── Module Entry Point ──────────────────────────────── */

#define ANGARA_ABI_PASTE_IMPL(a, b) a##b
#define ANGARA_ABI_PASTE(a, b) ANGARA_ABI_PASTE_IMPL(a, b)

/// Declare the module entry point function.
/// @param MODULE_NAME  The PascalCase module name (e.g., MyModule).
///   Expands to: const AngaraFuncDef* Angara_ModuleName_Init(int* def_count)
#define ANGARA_MODULE_INIT(MODULE_NAME) \
    const AngaraFuncDef* ANGARA_ABI_PASTE(Angara_, ANGARA_ABI_PASTE(MODULE_NAME, _Init))(int* def_count)


/* ==========================================================================
 * PRIVATE / INTERNAL API
 * ==========================================================================
 *
 * Everything below is intended for the transpiler-generated C code and the
 * runtime implementation files (rt_*.c).  Native module authors should
 * NOT use these symbols directly -- they are not part of the stable ABI
 * and may change between releases without notice.
 * ========================================================================== */


/*
  §P1  Threading & Concurrency Internals
  ────────────────────────────────────────
  These types and functions support Angara's spawn/join threading model.
  The transpiler generates calls to these; module authors should prefer
  Angara-level Thread/Mutex APIs.
*/

/// Managed pthread wrapper.
typedef struct AngaraThread {
    Object obj;
    pthread_t handle;
    AngaraObject return_value;  ///< Value returned by the thread's closure
} AngaraThread;

/// Managed pthread_mutex wrapper.
typedef struct AngaraMutex {
    Object obj;
    pthread_mutex_t handle;
} AngaraMutex;

/// Create a new mutex.
AngaraObject angara_mutex_new(void);

/// Lock a mutex (blocking).
void angara_mutex_lock(AngaraObject mutex_obj);

/// Unlock a mutex.
void angara_mutex_unlock(AngaraObject mutex_obj);

/// Spawn a new thread executing the given closure with arguments.
/// @param closure     An OBJ_CLOSURE to execute in the new thread.
/// @param arg_count   Number of arguments to pass.
/// @param args        Argument array.
/// @return            An OBJ_THREAD value.
AngaraObject angara_spawn_thread(AngaraObject closure, int arg_count, AngaraObject args[]);

/// Block until the given thread completes and return its result.
AngaraObject angara_thread_join(AngaraObject thread_obj);


/*
  §P2  Exception Handling Internals
  ──────────────────────────────────
  The transpiler uses setjmp/longjmp to implement try/catch.  These globals
  and functions manage the exception unwind chain.
*/

#define ANGARA_MAX_EXCEPTION_FRAMES 256

typedef struct ExceptionFrame {
    jmp_buf buffer;
    struct ExceptionFrame* prev;
} ExceptionFrame;

/// The head of the exception handler chain (thread-local in future).
extern ExceptionFrame* g_exception_chain_head;

/// The currently-caught exception object.
extern AngaraObject g_current_exception;

/// Begin a try block.  Returns the setjmp return value (0 = normal entry, 1 = caught).
int angara_try_begin(void);

/// End a try block (pops the exception frame).
void angara_try_end(void);

/// Throw an Angara exception object.  Does not return — longjmps to the nearest catch.
void angara_throw(AngaraObject exception);


/*
  §P3  Data & Enum VTable Layout
  ──────────────────────────────
  Data classes and enum variants share a common vtable pattern for
  equality and deep cloning.  These are generated by the transpiler.

  Note: AngaraDataInfo, AngaraEnumInfo, AngaraDataInstanceHeader, and
  AngaraEnumInstanceHeader are forward-declared above §12 so that the
  public inline type-check functions can reference them.
*/

/// VTable / metadata for a data struct.
struct AngaraDataInfo {
    const char* name;   ///< Data type name (e.g., "Point")
    bool (*equals_fn)(const void* a, const void* b);           ///< Structural equality
    AngaraObject (*deep_clone_fn)(const void* src);            ///< Deep clone
};

/// VTable / metadata for an enum type.
struct AngaraEnumInfo {
    const char* name;   ///< Enum type name (e.g., "Option")
    bool (*equals_fn)(const void* a, const void* b);           ///< Structural equality
    AngaraObject (*deep_clone_fn)(const void* src);            ///< Deep clone
};


/*
  §P4  Call Dispatch & Bound Methods
  ───────────────────────────────────
  The transpiler uses these to implement function calls, method dispatch,
  and bound method references.
*/

/// A bound method — a receiver + method closure pair.
typedef struct {
    Object obj;
    AngaraObject receiver;        ///< The 'this' object
    AngaraObject method_closure;  ///< The generic function wrapper
} AngaraBoundMethod;

/// Call an Angara closure or bound method.
/// @param callee     An OBJ_CLOSURE or OBJ_BOUND_METHOD value.
/// @param arg_count  Number of arguments.
/// @param args       Argument array.
AngaraObject angara_call(AngaraObject callee, int arg_count, AngaraObject args[]);

/// Create a new bound method (receiver + closure pair).
AngaraObject angara_bound_method_new(AngaraObject receiver, AngaraObject method_closure);


/*
  §P5  Instance Creation
  ──────────────────────
  Allocated by the transpiler for class instances.
*/

/// Allocate a new class instance of the given size.
/// @param size   Total allocation size including the AngaraInstance header and all fields.
/// @param klass  Pointer to the class metadata.
/// @return       A raw Object* to the new instance.
Object* angara_instance_new(size_t size, AngaraClass* klass);


/*
  §P6  Raw Pointer API
  ─────────────────────
  Convenience wrappers that operate on raw pointers instead of AngaraObject.
  Used by the transpiler for performance-sensitive paths.
*/

void angara_incref_ptr(void* ptr);
void angara_decref_ptr(void* ptr);
int64_t angara_len_ptr(void* collection);
void* angara_deep_clone_ptr(void* ptr);
const char* angara_string_concat_raw(const char* a, const char* b);
const char* angara_to_string_raw(AngaraObject val);

void* angara_list_get_raw(void* list, int64_t index);
void angara_list_push_raw(void* list, void* item);


/*
  §P7  Operator Helpers
  ─────────────────────
  Used by the transpiler for pre/post increment/decrement on i64 lvalues.
*/

AngaraObject angara_pre_increment(AngaraObject* lvalue);
AngaraObject angara_post_increment(AngaraObject* lvalue);
AngaraObject angara_pre_decrement(AngaraObject* lvalue);
AngaraObject angara_post_decrement(AngaraObject* lvalue);

/// Reinterpret a C pointer (stored as i64) as a data instance wrapper.
AngaraObject angara_retype_c_ptr(AngaraObject c_ptr_obj, size_t wrapper_size);


/*
  §P8  Runtime Lifecycle
  ──────────────────────
*/

/// Initialize the Angara runtime.  Called once at program start.
extern void angara_runtime_init(void);

/// Shut down the Angara runtime.  Called once at program exit.
extern void angara_runtime_shutdown(void);

#endif // ANGARA_H
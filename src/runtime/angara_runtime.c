//
// Created by cv2 on 02.09.2025.
//

#include "angara_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- Internal Forward Declarations ---
static void free_object(Object* object);
void printObject(AngaraObject obj);

// --- Internal Forward Declarations for Records ---
static void grow_record_capacity(AngaraRecord* record);
static void free_record(AngaraRecord* record);

// --- Value Constructors ---
AngaraObject angara_create_nil(void) { return (AngaraObject){VAL_NIL, {.i64 = 0}}; }
AngaraObject angara_create_bool(bool value) { return (AngaraObject){VAL_BOOL, {.boolean = value}}; }
AngaraObject angara_create_i64(int64_t value) { return (AngaraObject){VAL_I64, {.i64 = value}}; }
AngaraObject angara_create_f64(double value) { return (AngaraObject){VAL_F64, {.f64 = value}}; }

// --- Memory Management ---
void angara_incref(AngaraObject value) {
    if (IS_OBJ(value)) AS_OBJ(value)->ref_count++;
}
void angara_decref(AngaraObject value) {
    if (IS_OBJ(value)) {
        AS_OBJ(value)->ref_count--;
        if (AS_OBJ(value)->ref_count == 0) free_object(AS_OBJ(value));
    }
}

// --- Built-in Functions ---
void angara_print(int arg_count, AngaraObject args[]) {
    for (int i = 0; i < arg_count; ++i) {
        printObject(args[i]);
        if (i < arg_count - 1) printf(" ");
    }
    printf("\n");
}

AngaraObject angara_len(AngaraObject collection) {
    if (IS_OBJ(collection)) {
        if (OBJ_TYPE(collection) == OBJ_STRING) return angara_create_i64(AS_STRING(collection)->length);
        if (OBJ_TYPE(collection) == OBJ_LIST) return angara_create_i64(AS_LIST(collection)->count);
    }
    return angara_create_nil();
}

bool angara_is_truthy(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:
            return false;
        case VAL_BOOL:
            return AS_BOOL(value);
        case VAL_I64:
            return AS_I64(value) != 0;
        case VAL_F64:
            return AS_F64(value) != 0.0;
        case VAL_OBJ: {
            // For object types, we check if they are "empty".
            switch (OBJ_TYPE(value)) {
                case OBJ_STRING:
                    // An empty string is falsy.
                    return AS_STRING(value)->length > 0;
                case OBJ_LIST:
                    // An empty list is falsy.
                    return AS_LIST(value)->count > 0;
                case OBJ_RECORD:
                    // An empty record is falsy.
                    return AS_RECORD(value)->count > 0;

                    // All other object types (functions, instances, threads, etc.) are always truthy.
                default:
                    return true;
            }
        }
        default:
            return false; // Should be unreachable
    }
}

// --- String Implementation ---
AngaraObject angara_string_from_c(const char* chars) {
    size_t length = strlen(chars);
    AngaraString* string = (AngaraString*)malloc(sizeof(AngaraString));
    string->obj.type = OBJ_STRING;
    string->obj.ref_count = 1;
    string->length = length;
    string->chars = (char*)malloc(length + 1);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)string}};
}

// --- List Implementation ---
static void grow_list_capacity(AngaraList* list) {
    size_t old_capacity = list->capacity;
    list->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    list->elements = (AngaraObject*)realloc(list->elements, sizeof(AngaraObject) * list->capacity);
}

AngaraObject angara_list_new(void) {
    AngaraList* list = (AngaraList*)malloc(sizeof(AngaraList));
    list->obj.type = OBJ_LIST;
    list->obj.ref_count = 1;
    list->count = 0;
    list->capacity = 0;
    list->elements = NULL;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)list}};
}

AngaraObject angara_list_new_with_elements(size_t count, AngaraObject elements[]) {
    AngaraObject list_obj = angara_list_new();
    for (size_t i = 0; i < count; i++) {
        angara_list_push(list_obj, elements[i]);
    }
    return list_obj;
}

void angara_list_push(AngaraObject list_obj, AngaraObject value) {
    AngaraList* list = AS_LIST(list_obj);
    if (list->capacity < list->count + 1) grow_list_capacity(list);
    list->elements[list->count] = value;
    list->count++;
    angara_incref(value);
}

AngaraObject angara_list_get(AngaraObject list_obj, AngaraObject index_obj) {
    AngaraList* list = AS_LIST(list_obj);
    int64_t index = AS_I64(index_obj);
    if (index < 0 || index >= list->count) return angara_create_nil();
    AngaraObject value = list->elements[index];
    angara_incref(value);
    return value;
}

void angara_list_set(AngaraObject list_obj, AngaraObject index_obj, AngaraObject value) {
    AngaraList* list = AS_LIST(list_obj);
    int64_t index = AS_I64(index_obj);
    if (index < 0 || index >= list->count) return;
    angara_decref(list->elements[index]);
    list->elements[index] = value;
    angara_incref(value);
}

// --- Record Implementation (STUBBED) ---
AngaraObject angara_record_new(void) {
    AngaraRecord* record = (AngaraRecord*)malloc(sizeof(AngaraRecord));
    if (record == NULL) {
        // In a real-world scenario, handle allocation failure gracefully.
        exit(1);
    }
    record->obj.type = OBJ_RECORD;
    record->obj.ref_count = 1;
    record->count = 0;
    record->capacity = 0;
    record->entries = NULL;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)record}};
}

// Helper to grow the dynamic array of record entries.
static void grow_record_capacity(AngaraRecord* record) {
    size_t old_capacity = record->capacity;
    record->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    record->entries = (RecordEntry*)realloc(record->entries, sizeof(RecordEntry) * record->capacity);
    if (record->entries == NULL) {
        exit(1); // Handle allocation failure
    }
}

// Sets a field on a record. If the key already exists, it updates the value.
// Otherwise, it adds a new key-value pair.
void angara_record_set(AngaraObject record_obj, const char* key, AngaraObject value) {
    if (!IS_OBJ(record_obj) || OBJ_TYPE(record_obj) != OBJ_RECORD) return;
    AngaraRecord* record = AS_RECORD(record_obj);

    // 1. Check if the key already exists (linear search).
    for (size_t i = 0; i < record->count; i++) {
        if (strcmp(record->entries[i].key, key) == 0) {
            // Key found. Decref the old value and update it.
            angara_decref(record->entries[i].value);
            record->entries[i].value = value;
            angara_incref(value);
            return;
        }
    }

    // 2. Key not found. Add a new entry.
    // Ensure there is enough capacity.
    if (record->capacity < record->count + 1) {
        grow_record_capacity(record);
    }

    // 3. Add the new entry to the end.
    RecordEntry* entry = &record->entries[record->count];
    record->count++;

    // The record must own its keys. We must copy the provided string.
    entry->key = strdup(key);
    entry->value = value;
    angara_incref(value);
}

// Gets a field from a record. Returns nil if the key is not found.
AngaraObject angara_record_get(AngaraObject record_obj, const char* key) {
    if (!IS_OBJ(record_obj) || OBJ_TYPE(record_obj) != OBJ_RECORD) return angara_create_nil();
    AngaraRecord* record = AS_RECORD(record_obj);

    // Linearly search for the key.
    for (size_t i = 0; i < record->count; i++) {
        if (strcmp(record->entries[i].key, key) == 0) {
            // Found it. Incref the value before returning it.
            angara_incref(record->entries[i].value);
            return record->entries[i].value;
        }
    }

    // Not found.
    return angara_create_nil();
}

// The constructor used by the transpiler for record literals.
// `kvs` is an array of [key1, value1, key2, value2, ...].
AngaraObject angara_record_new_with_fields(size_t pair_count, AngaraObject kvs[]) {
    AngaraObject record_obj = angara_record_new();

    for (size_t i = 0; i < pair_count; i++) {
        AngaraObject key_obj = kvs[i * 2];
        AngaraObject value_obj = kvs[i * 2 + 1];

        // The key from a literal is always an AngaraString. We need its C chars.
        const char* key_cstr = AS_CSTRING(key_obj);

        angara_record_set(record_obj, key_cstr, value_obj);
    }

    // The key and value objects in the kvs array were temporary and owned by the
    // caller. `angara_record_set` has already incref'd the values and copied the
    // keys, so we don't need to do anything with the kvs array itself.

    return record_obj;
}

// --- Internal Helper Implementations ---
static void free_string(AngaraString* string) {
    free(string->chars);
    free(string);
}
static void free_list(AngaraList* list) {
    for (size_t i = 0; i < list->count; i++) angara_decref(list->elements[i]);
    free(list->elements);
    free(list);
}

// Update the memory manager and printer
static void free_mutex(AngaraMutex* mutex) {
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

// The memory cleanup function for a record object.
static void free_record(AngaraRecord* record) {
    for (size_t i = 0; i < record->count; i++) {
        // Free the copied key string.
        free(record->entries[i].key);
        // Decref the value AngaraObject.
        angara_decref(record->entries[i].value);
    }
    // Free the array of entries itself.
    free(record->entries);
    // Free the record struct.
    free(record);
}

// 2. Update the memory manager
static void free_exception(AngaraException* exc) {
    angara_decref(exc->message); // Release the reference to the message string
    free(exc);
}

static void free_object(Object* object) {
    // printf("-- freeing object of type %d --\n", object->type);
    switch (object->type) {
        case OBJ_STRING: free_string((AngaraString*)object); break;
        case OBJ_LIST: free_list((AngaraList*)object); break;
        case OBJ_RECORD: free_record((AngaraRecord*)object); break;
        case OBJ_INSTANCE:
            // For now, just free the memory. We'll need to handle
            // decref-ing all fields later.
            free(object);
            break;
        case OBJ_CLASS:
            // Classes can be global/static, may not need freeing,
            // or may need their name freed.
            free(((AngaraClass*)object)->name);
            free(object);
            break;
        case OBJ_MUTEX: free_mutex((AngaraMutex*)object); break; // <-- ADD THIS
        case OBJ_EXCEPTION: free_exception((AngaraException*)object); break;
        default: break;
    }
}

void printObject(AngaraObject obj) {
    switch (obj.type) {
        case VAL_NIL:   printf("nil"); break;
        case VAL_BOOL:  printf(AS_BOOL(obj) ? "true" : "false"); break;
        case VAL_I64:   printf("%lld", AS_I64(obj)); break;
        case VAL_F64:   printf("%g", AS_F64(obj)); break;
        case VAL_OBJ:
            switch (OBJ_TYPE(obj)) {
                case OBJ_STRING: printf("%s", AS_CSTRING(obj)); break;
                case OBJ_LIST: {
                    AngaraList* list = AS_LIST(obj);
                    printf("[");
                    for (size_t i = 0; i < list->count; i++) {
                        printObject(list->elements[i]);
                        if (i < list->count - 1) printf(", ");
                    }
                    printf("]");
                    break;
                }
                case OBJ_CLASS:
                    printf("<class %s>", AS_CLASS(obj)->name);
                    break;
                case OBJ_INSTANCE:
                    printf("<instance of %s>", AS_INSTANCE(obj)->klass->name);
                    break;
                case OBJ_THREAD: printf("<thread>"); break;
                case OBJ_MUTEX: printf("<mutex>"); break;
                case OBJ_RECORD: {
                    AngaraRecord* record = AS_RECORD(obj);
                    printf("{");
                    for (size_t i = 0; i < record->count; i++) {
                        printf("%s: ", record->entries[i].key);
                        printObject(record->entries[i].value);
                        if (i < record->count - 1) printf(", ");
                    }
                    printf("}");
                    break;
                }

            case OBJ_EXCEPTION: { // <-- ADD THIS
                    AngaraException* exc = AS_EXCEPTION(obj);
                    printf("Exception: %s", AS_CSTRING(exc->message));
                    break;
            }

                default: printf("<object>"); break;
            }
            break;
    }
}

Object* angara_instance_new(size_t size, AngaraClass* klass) {
    AngaraInstance* instance = (AngaraInstance*)malloc(size);
    if (instance == NULL) exit(1);

    instance->obj.type = OBJ_INSTANCE;
    instance->obj.ref_count = 1;
    instance->klass = klass; // Store the pointer to the class object

    return (Object*)instance;
}

// --- Exception Handling Implementation ---
AngaraObject g_current_exception;
jmp_buf g_exception_stack[ANGARA_MAX_EXCEPTION_FRAMES];
int g_exception_stack_top = 0;

int angara_try_begin(void) {
    if (g_exception_stack_top >= ANGARA_MAX_EXCEPTION_FRAMES) {
        printf("Exception stack overflow!\n");
        exit(1);
    }
    return setjmp(g_exception_stack[g_exception_stack_top++]);
}

void angara_try_end(void) {
    g_exception_stack_top--;
}

ExceptionFrame* g_exception_chain_head = NULL;

void angara_throw(AngaraObject exception) {
    if (g_exception_chain_head == NULL) {
        printf("Unhandled exception\n");
        exit(1);
    }
    g_current_exception = exception;
    angara_incref(g_current_exception);
    ExceptionFrame* frame = g_exception_chain_head;
    g_exception_chain_head = frame->prev; // Unlink
    longjmp(frame->buffer, 1);
}

void angara_runtime_init(void) {
    // For now, this is empty. But in the future, it could initialize
    // a memory manager, a thread pool, random number seeds, etc.

}

void angara_runtime_shutdown(void) {
    // This is where we would perform final cleanup, like ensuring all
    // allocated objects have been freed (a good way to detect memory leaks).
}


// The generic C function that a new pthread will execute.
// It's a simple wrapper that calls our Angara function.
void* thread_starter_routine(void* arg) {
    // The argument is a pointer to the AngaraThread object itself,
    // not just the closure. This allows us to store the return value.
    AngaraThread* thread = (AngaraThread*)arg;

    // Call the closure with no arguments.
    AngaraObject result = angara_call(thread->closure_to_run, 0, NULL);

    // Store the result in the thread object.
    thread->return_value = result;

    // We are done with our reference to the closure.
    angara_decref(thread->closure_to_run);

    return NULL;
}

AngaraObject angara_spawn(AngaraObject closure) {
    // 1. Create the AngaraThread object that we will return.
    AngaraThread* thread_obj = (AngaraThread*)malloc(sizeof(AngaraThread));
    thread_obj->obj.type = OBJ_THREAD;
    thread_obj->obj.ref_count = 1;
    thread_obj->closure_to_run = closure;
    thread_obj->return_value = angara_create_nil(); // Initialize return value to nil
    angara_incref(closure);

    // 2. Pass a pointer to the entire AngaraThread object to the new thread.
    if (pthread_create(&thread_obj->handle, NULL, &thread_starter_routine, thread_obj) != 0) {
        printf("Error: Failed to create Angara thread.\n");
        angara_decref(closure);
        free(thread_obj);
        return angara_create_nil();
    }

    return (AngaraObject){VAL_OBJ, {.obj = (Object*)thread_obj}};
}

AngaraObject angara_thread_join(AngaraObject thread_obj) {
    if (!IS_OBJ(thread_obj) || OBJ_TYPE(thread_obj) != OBJ_THREAD) {
        return angara_create_nil(); // Return nil on error
    }
    AngaraThread* thread = AS_THREAD(thread_obj);

    // 1. Wait for the C thread to finish its execution.
    pthread_join(thread->handle, NULL);

    // 2. Return the value that the thread stored.
    //    The caller now gets a reference to this value.
    angara_incref(thread->return_value);
    return thread->return_value;
}

AngaraObject angara_closure_new(GenericAngaraFn fn, int arity, bool is_native) {
    AngaraClosure* closure = (AngaraClosure*)malloc(sizeof(AngaraClosure));
    closure->obj.type = OBJ_CLOSURE;
    closure->obj.ref_count = 1;
    closure->fn = fn;
    closure->arity = arity;
    closure->is_native = is_native;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)closure}};
}

AngaraObject angara_call(AngaraObject closure_obj, int arg_count, AngaraObject args[]) {
    if (!IS_OBJ(closure_obj) || OBJ_TYPE(closure_obj) != OBJ_CLOSURE) {
        // This would be a runtime error.
        printf("Runtime Error: Attempted to call a non-function.\n");
        return angara_create_nil();
    }

    AngaraClosure* closure = AS_CLOSURE(closure_obj);

    // Arity check
    if (!closure->is_native && closure->arity != arg_count) {
        printf("Runtime Error: Expected %d arguments but got %d.\n", closure->arity, arg_count);
        return angara_create_nil();
    }

    // --- The actual call ---
    // We can call the function pointer directly.
    return closure->fn(arg_count, args);
}

AngaraObject angara_mutex_new(void) {
    AngaraMutex* mutex = (AngaraMutex*)malloc(sizeof(AngaraMutex));
    mutex->obj.type = OBJ_MUTEX;
    mutex->obj.ref_count = 1;

    // Initialize the underlying pthread mutex
    if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
        printf("Error: Failed to initialize mutex.\n");
        free(mutex);
        return angara_create_nil();
    }

    return (AngaraObject){VAL_OBJ, {.obj = (Object*)mutex}};
}

void angara_mutex_lock(AngaraObject mutex_obj) {
    if (!IS_OBJ(mutex_obj) || OBJ_TYPE(mutex_obj) != OBJ_MUTEX) return;
    pthread_mutex_lock(&AS_MUTEX(mutex_obj)->handle);
}

void angara_mutex_unlock(AngaraObject mutex_obj) {
    if (!IS_OBJ(mutex_obj) || OBJ_TYPE(mutex_obj) != OBJ_MUTEX) return;
    pthread_mutex_unlock(&AS_MUTEX(mutex_obj)->handle);
}

AngaraObject angara_pre_increment(AngaraObject* lvalue) {
    // Note: Assumes the type is i64 for simplicity. A real implementation
    // would check for floats as well.
    lvalue->as.i64++;
    // Returns the NEW value.
    angara_incref(*lvalue); // The caller gets a new reference.
    return *lvalue;
}

AngaraObject angara_post_increment(AngaraObject* lvalue) {
    // Returns the ORIGINAL value.
    AngaraObject original_value = angara_create_i64(lvalue->as.i64);
    lvalue->as.i64++;
    // No incref needed, create_i64 returns a fresh value.
    return original_value;
}

AngaraObject angara_pre_decrement(AngaraObject* lvalue) {
    lvalue->as.i64--;
    angara_incref(*lvalue);
    return *lvalue;
}

AngaraObject angara_post_decrement(AngaraObject* lvalue) {
    AngaraObject original_value = angara_create_i64(lvalue->as.i64);
    lvalue->as.i64--;
    return original_value;
}

AngaraObject angara_typeof(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:   return angara_string_from_c("nil");
        case VAL_BOOL:  return angara_string_from_c("bool");
        case VAL_I64:   return angara_string_from_c("i64");
        case VAL_F64:   return angara_string_from_c("f64");
        case VAL_OBJ:
            switch (OBJ_TYPE(value)) {
                case OBJ_STRING:   return angara_string_from_c("string");
                case OBJ_LIST:     return angara_string_from_c("list");
                case OBJ_RECORD:   return angara_string_from_c("record");
                case OBJ_CLOSURE:  return angara_string_from_c("function");
                case OBJ_CLASS:    return angara_string_from_c("class");
                case OBJ_INSTANCE: return angara_string_from_c("instance");
                case OBJ_THREAD:   return angara_string_from_c("Thread");
                case OBJ_MUTEX:    return angara_string_from_c("Mutex");
                case OBJ_EXCEPTION:return angara_string_from_c("Exception");
                default:           return angara_string_from_c("unknown object");
            }
        default:
            return angara_string_from_c("unknown");
    }
}

void angara_throw_error(const char* message) {
    // Create an Angara string from the error message and throw it.
    angara_throw(angara_string_from_c(message));
}

// And a more efficient string creator for the ABI.
AngaraObject angara_create_string_no_copy(char* chars, size_t length) {
    AngaraString* string = (AngaraString*)malloc(sizeof(AngaraString));
    string->obj.type = OBJ_STRING;
    string->obj.ref_count = 1;
    string->length = length;
    string->chars = chars; // Takes ownership of the pointer
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)string}};
}

AngaraObject angara_equals(AngaraObject a, AngaraObject b) {
    if (a.type != b.type) {
        // Special case: allow comparing any number to any other number
        if ((IS_I64(a) || IS_F64(a)) && (IS_I64(b) || IS_F64(b))) {
            return angara_create_bool(AS_F64(a) == AS_F64(b));
        }
        return angara_create_bool(false); // Different types are not equal
    }

    switch (a.type) {
        case VAL_NIL: return angara_create_bool(true);
        case VAL_BOOL: return angara_create_bool(AS_BOOL(a) == AS_BOOL(b));
        case VAL_I64: return angara_create_bool(AS_I64(a) == AS_I64(b));
        case VAL_F64: return angara_create_bool(AS_F64(a) == AS_F64(b));
        case VAL_OBJ:
            if (OBJ_TYPE(a) == OBJ_STRING) {
                return angara_create_bool(strcmp(AS_CSTRING(a), AS_CSTRING(b)) == 0);
            }
            // For other objects, compare pointers for now.
            return angara_create_bool(AS_OBJ(a) == AS_OBJ(b));
    }
    return angara_create_bool(false);
}

AngaraObject angara_to_string(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:
            return angara_string_from_c("nil");
        case VAL_BOOL:
            return angara_string_from_c(AS_BOOL(value) ? "true" : "false");
        case VAL_I64: {
            // Determine required size and allocate a buffer for the string.
            // A 64-bit integer can be up to 20 digits long, plus sign and null terminator.
            char buffer[22];
            int len = snprintf(buffer, sizeof(buffer), "%lld", AS_I64(value));
            return angara_create_string_no_copy(strdup(buffer), len);
        }
        case VAL_F64: {
            // Use snprintf for safe float-to-string conversion.
            char buffer[32]; // Sufficient for most float representations
            int len = snprintf(buffer, sizeof(buffer), "%g", AS_F64(value));
            return angara_create_string_no_copy(strdup(buffer), len);
        }
        case VAL_OBJ: {
            // If it's already a string, just incref it and return a new reference.
            if (OBJ_TYPE(value) == OBJ_STRING) {
                angara_incref(value);
                return value;
            }
            // --- NEW: Special handling for Exception objects ---
            if (OBJ_TYPE(value) == OBJ_EXCEPTION) {
                AngaraException* exc = AS_EXCEPTION(value);
                // The message is already an AngaraString, so we can just return it.
                angara_incref(exc->message);
                return exc->message;
            }
            // --- END NEW ---
            // For other object types, return a placeholder representation.
            // We can expand this later.
            AngaraObject type_name_obj = angara_typeof(value);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "<%s object>", AS_CSTRING(type_name_obj));
            angara_decref(type_name_obj);
            return angara_string_from_c(buffer);
        }


        default:
            return angara_string_from_c("unknown");
    }
}

// Converts any AngaraObject into a new AngaraObject of type VAL_I64.
AngaraObject angara_to_i64(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:
            return angara_create_i64(0);
        case VAL_BOOL:
            return angara_create_i64(AS_BOOL(value) ? 1 : 0);
        case VAL_I64:
            // No conversion needed, just return a new reference.
            angara_incref(value);
            return value;
        case VAL_F64:
            // Truncates the float.
            return angara_create_i64((int64_t)AS_F64(value));
        case VAL_OBJ: {
            if (OBJ_TYPE(value) == OBJ_STRING) {
                // Use standard C function to parse string to long long.
                return angara_create_i64(strtoll(AS_CSTRING(value), NULL, 10));
            }
            // Other object types convert to 0 for now.
            return angara_create_i64(0);
        }
        default:
             return angara_create_i64(0);
    }
}

// Converts any AngaraObject into a new AngaraObject of type VAL_F64.
AngaraObject angara_to_f64(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:
            return angara_create_f64(0.0);
        case VAL_BOOL:
            return angara_create_f64(AS_BOOL(value) ? 1.0 : 0.0);
        case VAL_I64:
            return angara_create_f64((double)AS_I64(value));
        case VAL_F64:
            // No conversion needed, return new reference.
            angara_incref(value);
            return value;
        case VAL_OBJ: {
            if (OBJ_TYPE(value) == OBJ_STRING) {
                // Use standard C function to parse string to double.
                return angara_create_f64(strtod(AS_CSTRING(value), NULL));
            }
            return angara_create_f64(0.0);
        }
        default:
             return angara_create_f64(0.0);
    }
}

// Explicitly converts a value to a boolean. This is different from
// angara_is_truthy, which is used for implicit truthiness checks in `if`.
// This function follows stricter rules for explicit `bool()` casts.
AngaraObject angara_to_bool(AngaraObject value) {
    // The explicit bool() conversion is identical to the implicit truthiness check.
    // We can just reuse the existing function.
    return angara_create_bool(angara_is_truthy(value));
}

AngaraObject angara_string_concat(AngaraObject a, AngaraObject b) {
    // This function assumes type checking has already been done.
    AngaraString* s1 = AS_STRING(a);
    AngaraString* s2 = AS_STRING(b);

    size_t new_len = s1->length + s2->length;
    char* new_chars = (char*)malloc(new_len + 1);
    if (!new_chars) {
        // In a real runtime, this should probably throw a proper out-of-memory error.
        return angara_create_nil();
    }

    // Copy the first string's content
    memcpy(new_chars, s1->chars, s1->length);
    // Copy the second string's content right after the first
    memcpy(new_chars + s1->length, s2->chars, s2->length);
    new_chars[new_len] = '\0';

    // The new string object takes ownership of the buffer.
    return angara_create_string_no_copy(new_chars, new_len);
}

AngaraObject angara_record_get_with_angara_key(AngaraObject record_obj, AngaraObject key_obj) {
    if (!IS_STRING(key_obj)) return angara_create_nil();
    return angara_record_get(record_obj, AS_CSTRING(key_obj));
}

void angara_record_set_with_angara_key(AngaraObject record_obj, AngaraObject key_obj, AngaraObject value_obj) {
    // This is safe because the transpiler will only generate calls to this
    // if the key is a string. We can add a check for safety.
    if (IS_STRING(key_obj)) {
        angara_record_set(record_obj, AS_CSTRING(key_obj), value_obj);
    }
}

AngaraObject angara_exception_new(AngaraObject message) {
    if (!IS_STRING(message)) {
        // Fallback for safety, should be caught by type checker
        message = angara_string_from_c("Non-string value provided to Exception constructor.");
    }

    AngaraException* exc = (AngaraException*)malloc(sizeof(AngaraException));
    exc->obj.type = OBJ_EXCEPTION;
    exc->obj.ref_count = 1;
    exc->message = message;
    angara_incref(message); // The exception now holds a reference to the message

    return (AngaraObject){VAL_OBJ, {.obj = (Object*)exc}};
}
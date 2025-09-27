//
// Created by cv2 on 02.09.2025.
//

#include "angara_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ANSI_COLOR_BOLD_RED   "\033[1;31m"
#define ANSI_COLOR_YELLOW     "\033[0;33m"
#define ANSI_COLOR_CYAN       "\033[0;36m"
#define ANSI_COLOR_RESET      "\033[0m"

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

AngaraObject angara_create_native_instance(void* data, AngaraFinalizerFn finalizer) {
    AngaraNativeInstance* instance = (AngaraNativeInstance*)malloc(sizeof(AngaraNativeInstance));
    instance->obj.type = OBJ_NATIVE_INSTANCE;
    instance->obj.ref_count = 1;
    instance->data = data;
    instance->finalizer = finalizer;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)instance}};
}

static void free_native_instance(AngaraNativeInstance* instance) {
    // If the module provided a finalizer, call it with the custom data pointer.
    if (instance->finalizer) {
        instance->finalizer(instance->data);
    }
    // Then, free the container struct itself.
    free(instance);
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
        case OBJ_NATIVE_INSTANCE: free_native_instance((AngaraNativeInstance*)object); break;
        case OBJ_DATA_INSTANCE: free(object); break;
        case OBJ_INSTANCE:
            // For now, just free the memory. We'll need to handle
            // decref-ing all fields later. TODO
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
            // An enum instance might hold AngaraObjects in its payload. We MUST decref them.
            // This is a complex task. For now, we will assume a simple free, but this
            // will cause memory leaks if a variant holds a string, list, etc.
            // TODO: Implement a GC-aware free for enums.
        case OBJ_ENUM_INSTANCE: free(object); break;
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
                case OBJ_DATA_INSTANCE: { // <-- ADD THIS
                        // We don't know the type name at runtime yet, so just print a placeholder.
                        // This can be improved later by storing a pointer to the DataType. // TODO
                        printf("<data object>");
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
            case OBJ_ENUM_INSTANCE: {
                    // We don't have enough runtime info to print the variant name yet.
                    // TODO
                    printf("<enum instance>");
                    break;
            }
                case OBJ_NATIVE_INSTANCE:
                    printf("<native instance at %p>", AS_NATIVE_INSTANCE(obj)->data);
                    break;

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

void angara_debug_print(const char* message) {
    // We use fprintf to stderr to make sure it's not buffered
    // and doesn't interfere with the program's stdout.
    fprintf(stderr, "[DEBUG] %s\n", message);
}

// 2. Enhance the angara_throw function
void angara_throw(AngaraObject exception) {
    // This function is called for ALL throws. It only terminates if there's
    // no active `try` block to jump to.

    if (g_exception_chain_head == NULL) {
        // --- This is the UNHANDLED exception path ---

        fprintf(stderr, "\n" ANSI_COLOR_BOLD_RED "[FATAL] Unhandled Angara Exception" ANSI_COLOR_RESET "\n");

        if (IS_OBJ(exception) && OBJ_TYPE(exception) == OBJ_EXCEPTION) {
            // The exception is a proper, standard Exception object.
            fprintf(stderr, ANSI_COLOR_YELLOW "  -> Message: " ANSI_COLOR_RESET "%s\n", AS_CSTRING(AS_EXCEPTION(exception)->message));
        } else if (IS_OBJ(exception) && OBJ_TYPE(exception) == OBJ_STRING) {
            // It was a raw string, likely from an old angara_throw_error call.
            fprintf(stderr, ANSI_COLOR_YELLOW "  -> Message: " ANSI_COLOR_RESET "%s\n", AS_CSTRING(exception));
        } else {
            // It's some other non-standard object.
            fprintf(stderr, ANSI_COLOR_YELLOW "  -> Thrown object was not a standard Exception or String type." ANSI_COLOR_RESET "\n");
        }

        fprintf(stderr, ANSI_COLOR_CYAN "  -> No active `try` blocks were found on the call stack. Terminating program." ANSI_COLOR_RESET "\n\n");
        exit(1);
    }

    // If there IS a try block, we perform the longjmp to it.
    g_current_exception = exception;
    angara_incref(g_current_exception);
    ExceptionFrame* frame = g_exception_chain_head;
    g_exception_chain_head = frame->prev;
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

typedef struct {
    AngaraObject closure;
    int arg_count;
    AngaraObject* args; // A dynamically allocated array of arguments
} ThreadStartData;


// The generic C function that a new pthread will execute.
// It's a simple wrapper that calls our Angara function.
void* thread_starter_routine(void* arg) {
    const ThreadStartData* start_data = (ThreadStartData*)arg;
    AngaraThread* thread_obj = (AngaraThread*)start_data->args[0].as.obj;

    const AngaraObject result = angara_call(start_data->closure, start_data->arg_count - 1, start_data->args + 1);
    thread_obj->return_value = result;

    // Cleanup
    angara_decref(start_data->closure);
    for (int i = 0; i < start_data->arg_count; ++i) {
        angara_decref(start_data->args[i]);
    }
    free(start_data->args);
    free(start_data);
    return NULL;
}

AngaraObject angara_spawn_thread(AngaraObject closure, int arg_count, AngaraObject args[]) {
    // 1. Create the ThreadStartData struct that will be passed to the new thread.
    ThreadStartData* start_data = (ThreadStartData*)malloc(sizeof(ThreadStartData));
    if (!start_data) { /* out of memory */ return angara_create_nil(); }

    start_data->closure = closure;
    start_data->arg_count = arg_count + 1; // +1 for the thread object itself
    angara_incref(closure);


    // 2. Allocate a NEW array on the HEAP for the arguments.
    start_data->args = (AngaraObject*)malloc(sizeof(AngaraObject) * start_data->arg_count);
    if (!start_data->args) { /* out of memory */ free(start_data); return angara_create_nil(); }

    // 3. Create the AngaraThread object that we will return to the user.
    AngaraThread* thread_obj = (AngaraThread*)malloc(sizeof(AngaraThread));
    // ... (check malloc) ...
    thread_obj->obj.type = OBJ_THREAD;
    thread_obj->obj.ref_count = 1;
    thread_obj->return_value = angara_create_nil();
    AngaraObject thread_angara_obj = { VAL_OBJ, { .obj = (Object*)thread_obj }};

    // 4. Copy the arguments from the temporary stack array into our new heap array.

    // The first argument is always the thread object itself.
    start_data->args[0] = thread_angara_obj;
    angara_incref(thread_angara_obj); // The start_data now holds a reference

    // Copy the rest of the arguments from the caller.
    for (int i = 0; i < arg_count; ++i) {
        start_data->args[i + 1] = args[i];
        angara_incref(args[i]); // The start_data now holds a reference
    }


    // 5. Create the C pthread, passing the HEAP-allocated start_data.
    if (pthread_create(&thread_obj->handle, NULL, &thread_starter_routine, start_data) != 0) {
        // Complex cleanup required here on failure
        angara_decref(start_data->closure);
        for(int i = 0; i < start_data->arg_count; ++i) angara_decref(start_data->args[i]);
        free(start_data->args);
        free(start_data);
        free(thread_obj); // Not an Angara object yet, just free it
        angara_throw_error("Failed to create new thread.");
        return angara_create_nil();
    }

    // The thread is running. Return the handle to the user.
    return thread_angara_obj;
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
        angara_throw_error("Runtime Error: Attempted to call a non-function value.");
        return angara_create_nil();
    }

    AngaraClosure* closure = AS_CLOSURE(closure_obj);


    // Arity check for non-native, non-variadic functions.
    // A native function's arity check is handled by the C code itself.
    // An arity of -1 means it's variadic.
    if (!closure->is_native && closure->arity != -1 && closure->arity != arg_count) {
        char error_buf[256];
        sprintf(error_buf, "Runtime Error: Arity mismatch. Function expected %d argument(s) but received %d.",
                closure->arity, arg_count);
        angara_throw_error(error_buf);
        return angara_create_nil();
    }

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
    // 1. First, create an AngaraString for the message.
    AngaraObject message_obj = angara_string_from_c(message);

    // 2. Then, create a proper AngaraException that WRAPS the message.
    AngaraObject exception_obj = angara_exception_new(message_obj);

    // 3. The message object was consumed by angara_exception_new (its ref_count was
    //    incremented), so we can now decref our local reference to it.
    angara_decref(message_obj);

    // 4. Finally, throw the correctly-typed exception object.
    angara_throw(exception_obj);
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

AngaraObject angara_create_string(const char* chars) {
    size_t length = strlen(chars);

    // 1. Allocate a new buffer and copy the string data into it.
    //    The new AngaraString will own this new buffer.
    char* heap_chars = (char*)malloc(length + 1);
    if (!heap_chars) {
        // In a real-world scenario, you might want a more graceful
        // out-of-memory error, but for now this is okay.
        return angara_create_nil();
    }
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';

    // 2. Call the no-copy version to do the final object creation.
    //    This avoids duplicating the object allocation logic.
    return angara_create_string_no_copy(heap_chars, length);
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
        // this should probably throw a proper out-of-memory error.
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

AngaraObject angara_list_remove_at(AngaraObject list_obj, AngaraObject index_obj) {
    if (!IS_LIST(list_obj) || !IS_I64(index_obj)) {
        angara_throw_error("remove_at(list, index) received invalid arguments.");
        return angara_create_nil();
    }
    AngaraList* list = AS_LIST(list_obj);
    int64_t index = AS_I64(index_obj);

    if (index < 0 || (size_t)index >= list->count) {
        angara_throw_error("List index out of bounds for remove_at().");
        return angara_create_nil();
    }

    // 1. Get the value to be returned. We don't incref, as we are
    //    transferring the list's ownership to the caller.
    AngaraObject removed_value = list->elements[index];

    // 2. Shift all subsequent elements one position to the left.
    //    memmove is the safe choice for overlapping memory regions.
    if (list->count > 1 && (size_t)index < list->count - 1) {
        memmove(&list->elements[index],
                &list->elements[index + 1],
                (list->count - index - 1) * sizeof(AngaraObject));
    }

    // 3. Decrease the list's count.
    list->count--;

    return removed_value;
}

// Searches for and removes the first occurrence of a value in the list.
// This requires a way to check for equality between AngaraObjects.
AngaraObject angara_list_remove(AngaraObject list_obj, AngaraObject value_to_remove) {
    if (!IS_LIST(list_obj)) {
        angara_throw_error("remove(list, value) received invalid list argument.");
        return angara_create_nil();
    }
    AngaraList* list = AS_LIST(list_obj);

    // 1. Find the index of the value.
    int64_t found_index = -1;
    for (size_t i = 0; i < list->count; ++i) {
        // We reuse the runtime's equality function.
        if (AS_BOOL(angara_equals(list->elements[i], value_to_remove))) {
            found_index = (int64_t)i;
            break;
        }
    }

    // 2. If found, call our existing remove_at logic.
    if (found_index != -1) {
        AngaraObject removed = angara_list_remove_at(list_obj, angara_create_i64(found_index));
        // We are the final owner of the removed value, so we must decref it.
        angara_decref(removed);
        return angara_create_bool(true); // An item was removed.
    }

    return angara_create_bool(false); // No item was removed.
}

AngaraObject angara_record_remove(AngaraObject record_obj, AngaraObject key_obj) {
    if (!IS_RECORD(record_obj) || !IS_STRING(key_obj)) {
        // This check is for safety; the type checker should prevent this.
        return angara_create_bool(false);
    }
    AngaraRecord* record = AS_RECORD(record_obj);
    const char* key_to_remove = AS_CSTRING(key_obj);

    // 1. Find the index of the key.
    int64_t found_index = -1;
    for (size_t i = 0; i < record->count; ++i) {
        if (strcmp(record->entries[i].key, key_to_remove) == 0) {
            found_index = (int64_t)i;
            break;
        }
    }

    if (found_index == -1) {
        return angara_create_bool(false); // Key not found.
    }

    // 2. Free the key and decref the value of the entry to be removed.
    free(record->entries[found_index].key);
    angara_decref(record->entries[found_index].value);

    // 3. Shift all subsequent elements one position to the left.
    if (record->count > 1 && (size_t)found_index < record->count - 1) {
        memmove(&record->entries[found_index],
                &record->entries[found_index + 1],
                (record->count - found_index - 1) * sizeof(RecordEntry));
    }

    // 4. Decrease the record's count.
    record->count--;

    return angara_create_bool(true); // Success.
}

// Returns a new list containing all the keys of a record.
AngaraObject angara_record_keys(AngaraObject record_obj) {
    if (!IS_RECORD(record_obj)) {
        return angara_list_new(); // Return empty list on error
    }
    AngaraRecord* record = AS_RECORD(record_obj);

    // Create a new Angara list to store the keys.
    AngaraObject keys_list = angara_list_new();

    for (size_t i = 0; i < record->count; ++i) {
        // Create a new Angara string for each key and push it to the list.
        angara_list_push(keys_list, angara_create_string(record->entries[i].key));
    }

    return keys_list;
}

AngaraObject angara_is_instance_of(AngaraObject object, const char* type_name) {
    bool result = false;

    // First, check against primitive type names.
    switch (object.type) {
        case VAL_NIL:
            result = (strcmp(type_name, "nil") == 0);
            break;
        case VAL_BOOL:
            result = (strcmp(type_name, "bool") == 0);
            break;
        case VAL_I64:
            result = (strcmp(type_name, "i64") == 0 || strcmp(type_name, "int") == 0);
            break;
        case VAL_F64:
            result = (strcmp(type_name, "f64") == 0 || strcmp(type_name, "float") == 0);
            break;
        case VAL_OBJ: {
            // If it's an object, we check its internal object type.
            switch (OBJ_TYPE(object)) {
                case OBJ_STRING:
                    result = (strcmp(type_name, "string") == 0);
                    break;
                case OBJ_LIST:
                    // For now, we only check the base type "list". A full implementation
                    // would need to check the element type, which requires more runtime info.
                    // TODO - refine
                    result = (strcmp(type_name, "list") == 0);
                    break;
                case OBJ_RECORD:
                    result = (strcmp(type_name, "record") == 0);
                    break;
                case OBJ_NATIVE_INSTANCE:
                case OBJ_INSTANCE:
                    // This is the key case for user-defined types.
                    // We check the instance's class's name.
                    result = (strcmp(AS_INSTANCE(object)->klass->name, type_name) == 0);
                    break;
                    // Add cases for Exception, Thread, Mutex etc. as needed.
                default:
                    result = false;
                    break;
            }
            break;
        }
    }
    return angara_create_bool(result);
}

// Checks if an object is a list and if its elements match a given type name.
// Note: This is an expensive operation as it may have to check every element.
AngaraObject angara_is_list_of_type(AngaraObject list_obj, const char* element_type_name) {
    if (!IS_LIST(list_obj)) {
        return angara_create_bool(false);
    }
    AngaraList* list = AS_LIST(list_obj);

    // If the list is empty, it can be considered a list of any type.
    if (list->count == 0) {
        return angara_create_bool(true);
    }

    // To be correct, we should check the type of every element.
    // However, since Angara lists are homogeneous, we only need to check the first one.
    AngaraObject first_element = list->elements[0];

    // We can call our existing `angara_is_instance_of` on the element.
    return angara_is_instance_of(first_element, element_type_name);
}

AngaraObject angara_create_string_with_len(const char* chars, size_t length) {
    char* heap_chars = (char*)malloc(length + 1);
    if (!heap_chars) { return angara_create_nil(); }
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';
    return angara_create_string_no_copy(heap_chars, length);
}

AngaraObject angara_from_c_i32(int32_t value) { return angara_create_i64((int64_t)value); }
AngaraObject angara_from_c_u32(uint32_t value) { return angara_create_i64((int64_t)value); } // Note: Potential signedness issues, but i64 is safest container TODO
AngaraObject angara_from_c_i64(int64_t value) { return angara_create_i64(value); }
AngaraObject angara_from_c_f64(double value) { return angara_create_f64(value); }
AngaraObject angara_from_c_bool(bool value) { return angara_create_bool(value); }
AngaraObject angara_from_c_string(const char* value) { return angara_string_from_c(value); }
AngaraObject angara_from_c_c_ptr(void* value) { return angara_create_i64((int64_t)value); }
AngaraObject angara_from_c_u64(uint64_t value) { return angara_create_i64((int64_t)value); }

AngaraObject angara_retype_c_ptr(AngaraObject c_ptr_obj, size_t wrapper_size) {
    if (c_ptr_obj.type != VAL_I64 && c_ptr_obj.type != VAL_NIL) {
        // A safety check, though the type checker should prevent this.
        // A c_ptr is stored in the i64 slot.
        return angara_create_nil();
    }

    // 1. Allocate memory for our Angara wrapper struct (e.g., Angara_utsname).
    Object* wrapper_obj = (Object*)malloc(wrapper_size);
    if (!wrapper_obj) {
        angara_throw_error("Out of memory during retype operation.");
        return angara_create_nil();
    }

    // 2. Initialize the generic Angara object header.
    wrapper_obj->type = OBJ_DATA_INSTANCE; // We can reuse this type tag.
    wrapper_obj->ref_count = 1;

    // 3. This is the crucial step: Store the raw C pointer from the source
    //    object into the `ptr` field of the new wrapper. We assume the `ptr`
    //    field is the first field after the `Object obj` header.
    //    This is a bit of a hack but avoids needing a unique function for every type.
    void** ptr_field = (void**)((char*)wrapper_obj + sizeof(Object));
    *ptr_field = (void*)AS_I64(c_ptr_obj);

    // 4. Box the new wrapper struct into an AngaraObject and return it.
    return (AngaraObject){VAL_OBJ, {.obj = wrapper_obj}};
}
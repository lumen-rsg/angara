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

// --- Value Constructors ---
AngaraObject create_nil(void) { return (AngaraObject){VAL_NIL, {.i64 = 0}}; }
AngaraObject create_bool(bool value) { return (AngaraObject){VAL_BOOL, {.boolean = value}}; }
AngaraObject create_i64(int64_t value) { return (AngaraObject){VAL_I64, {.i64 = value}}; }
AngaraObject create_f64(double value) { return (AngaraObject){VAL_F64, {.f64 = value}}; }

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
        if (OBJ_TYPE(collection) == OBJ_STRING) return create_i64(AS_STRING(collection)->length);
        if (OBJ_TYPE(collection) == OBJ_LIST) return create_i64(AS_LIST(collection)->count);
    }
    return create_nil();
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
    if (index < 0 || index >= list->count) return create_nil();
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
AngaraObject angara_record_new(void) { /* ... */ return create_nil(); }
AngaraObject angara_record_new_with_fields(size_t pair_count, AngaraObject kvs[]) { /* ... */ return create_nil(); }
void angara_record_set(AngaraObject record, const char* key, AngaraObject value) { /* ... */ }
AngaraObject angara_record_get(AngaraObject record, const char* key) { /* ... */ return create_nil(); }


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

static void free_object(Object* object) {
    // printf("-- freeing object of type %d --\n", object->type);
    switch (object->type) {
        case OBJ_STRING: free_string((AngaraString*)object); break;
        case OBJ_LIST: free_list((AngaraList*)object); break;
        case OBJ_RECORD: /* TODO */ free(object); break;
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
                case OBJ_MUTEX: printf("<mutex>"); break; // <-- ADD THIS

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
    thread_obj->return_value = create_nil(); // Initialize return value to nil
    angara_incref(closure);

    // 2. Pass a pointer to the entire AngaraThread object to the new thread.
    if (pthread_create(&thread_obj->handle, NULL, &thread_starter_routine, thread_obj) != 0) {
        printf("Error: Failed to create Angara thread.\n");
        angara_decref(closure);
        free(thread_obj);
        return create_nil();
    }

    return (AngaraObject){VAL_OBJ, {.obj = (Object*)thread_obj}};
}

AngaraObject angara_thread_join(AngaraObject thread_obj) {
    if (!IS_OBJ(thread_obj) || OBJ_TYPE(thread_obj) != OBJ_THREAD) {
        return create_nil(); // Return nil on error
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
        return create_nil();
    }

    AngaraClosure* closure = AS_CLOSURE(closure_obj);

    // Arity check
    if (!closure->is_native && closure->arity != arg_count) {
        printf("Runtime Error: Expected %d arguments but got %d.\n", closure->arity, arg_count);
        return create_nil();
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
        return create_nil();
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
    AngaraObject original_value = create_i64(lvalue->as.i64);
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
    AngaraObject original_value = create_i64(lvalue->as.i64);
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
            return create_bool(AS_F64(a) == AS_F64(b));
        }
        return create_bool(false); // Different types are not equal
    }

    switch (a.type) {
        case VAL_NIL: return create_bool(true);
        case VAL_BOOL: return create_bool(AS_BOOL(a) == AS_BOOL(b));
        case VAL_I64: return create_bool(AS_I64(a) == AS_I64(b));
        case VAL_F64: return create_bool(AS_F64(a) == AS_F64(b));
        case VAL_OBJ:
            if (OBJ_TYPE(a) == OBJ_STRING) {
                return create_bool(strcmp(AS_CSTRING(a), AS_CSTRING(b)) == 0);
            }
            // For other objects, compare pointers for now.
            return create_bool(AS_OBJ(a) == AS_OBJ(b));
    }
    return create_bool(false);
}
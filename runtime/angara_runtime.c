//
// Created by cv2 on 02.09.2025.
//

#include "angara_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // For fmod

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
        case VAL_NIL: return false;
        case VAL_BOOL: return AS_BOOL(value);
        case VAL_I64: return AS_I64(value) != 0;
        case VAL_F64: return AS_F64(value) != 0.0;
        case VAL_OBJ: return true; // All objects are truthy for now
        default: return false;
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
                default: printf("<object>"); break;
            }
            break;
    }
}

Object* angara_instance_new(size_t size, AngaraClass* klass) {
    AngaraInstance* instance = (AngaraInstance*)malloc(size);
    if (instance == NULL) exit(1);

    // --- THIS IS THE FIX ---
    instance->obj.type = OBJ_INSTANCE;
    instance->obj.ref_count = 1;
    instance->klass = klass; // Store the pointer to the class object
    // --- END OF FIX ---

    return (Object*)instance;
}

// --- NEW: Exception Handling Implementation ---
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
    printf("-- Angara Runtime Initialized --\n");
}

void angara_runtime_shutdown(void) {
    // This is where we would perform final cleanup, like ensuring all
    // allocated objects have been freed (a good way to detect memory leaks).
    printf("-- Angara Runtime Shutdown --\n");
}



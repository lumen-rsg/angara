//
// Angara Runtime — Core: Value constructors, memory management, truthiness
//

#include "rt_internal.h"

// --- Value Constructors ---
AngaraObject angara_create_nil(void) {
    return (AngaraObject){VAL_NIL, {.i64 = 0}};
}

AngaraObject angara_create_bool(bool value) {
    return (AngaraObject){VAL_BOOL, {.boolean = value}};
}

AngaraObject angara_create_i64(int64_t value) {
    return (AngaraObject){VAL_I64, {.i64 = value}};
}

AngaraObject angara_create_f64(double value) {
    return (AngaraObject){VAL_F64, {.f64 = value}};
}

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

void angara_incref_ptr(void* ptr) {
    if (ptr) ((Object*)ptr)->ref_count++;
}

void angara_decref_ptr(void* ptr) {
    if (ptr) {
        Object* o = (Object*)ptr;
        o->ref_count--;
        if (o->ref_count == 0) free_object(o);
    }
}

// --- Truthiness ---
bool angara_is_truthy(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:   return false;
        case VAL_BOOL:  return AS_BOOL(value);
        case VAL_I64:   return AS_I64(value) != 0;
        case VAL_F64:   return AS_F64(value) != 0.0;
        case VAL_OBJ: {
            switch (OBJ_TYPE(value)) {
                case OBJ_STRING: return AS_STRING(value)->length > 0;
                case OBJ_LIST:   return AS_LIST(value)->count > 0;
                case OBJ_RECORD: return AS_RECORD(value)->count > 0;
                default:         return true;
            }
        }
        default: return false;
    }
}

// --- Object Freeing ---
static void free_string(AngaraString* string) {
    free(string->chars);
    free(string);
}

static void free_list(AngaraList* list) {
    for (size_t i = 0; i < list->count; i++) angara_decref(list->elements[i]);
    free(list->elements);
    free(list);
}

static void free_record(AngaraRecord* record) {
    for (size_t i = 0; i < record->count; i++) {
        free(record->entries[i].key);
        angara_decref(record->entries[i].value);
    }
    free(record->entries);
    free(record);
}

static void free_mutex(AngaraMutex* mutex) {
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

static void free_native_instance(AngaraNativeInstance* instance) {
    if (instance->finalizer) {
        instance->finalizer(instance->data);
    }
    free(instance);
}

static void free_exception(AngaraException* exc) {
    angara_decref(exc->message);
    free(exc);
}

void free_object(Object* object) {
    switch (object->type) {
        case OBJ_STRING:         free_string((AngaraString*)object); break;
        case OBJ_LIST:           free_list((AngaraList*)object); break;
        case OBJ_RECORD:         free_record((AngaraRecord*)object); break;
        case OBJ_NATIVE_INSTANCE: free_native_instance((AngaraNativeInstance*)object); break;
        case OBJ_DATA_INSTANCE:  free(object); break;
        case OBJ_INSTANCE:       free(object); break;
        case OBJ_CLASS:
            free(((AngaraClass*)object)->name);
            free(object);
            break;
        case OBJ_MUTEX:          free_mutex((AngaraMutex*)object); break;
        case OBJ_EXCEPTION:      free_exception((AngaraException*)object); break;
        case OBJ_ENUM_INSTANCE:  free(object); break;
        case OBJ_BOUND_METHOD: {
            AngaraBoundMethod* bm = (AngaraBoundMethod*)object;
            angara_decref(bm->receiver);
            angara_decref(bm->method_closure);
            free(bm);
            break;
        }
        default: break;
    }
}

// --- Instance Creation ---
Object* angara_instance_new(size_t size, AngaraClass* klass) {
    AngaraInstance* instance = (AngaraInstance*)malloc(size);
    if (instance == NULL) {
        fprintf(stderr, "Out of memory: failed to allocate instance.\n");
        exit(1);
    }
    instance->obj.type = OBJ_INSTANCE;
    instance->obj.ref_count = 1;
    instance->klass = klass;
    return (Object*)instance;
}

// --- Runtime Lifecycle ---
void angara_runtime_init(void) {
    // Reserved for future initialization (memory manager, thread pool, etc.)
}

void angara_runtime_shutdown(void) {
    // Reserved for final cleanup and memory leak detection.
}

void angara_debug_print(const char* message) {
    fprintf(stderr, "[DEBUG] %s\n", message);
}
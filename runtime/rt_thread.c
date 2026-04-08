//
// Angara Runtime — Threading and concurrency
//

#include "rt_internal.h"

// --- Thread Start Data (internal) ---
typedef struct {
    AngaraObject closure;
    int arg_count;
    AngaraObject* args;
} ThreadStartData;

// --- Thread Entry Point ---
static void* thread_starter_routine(void* arg) {
    ThreadStartData* start_data = (ThreadStartData*)arg;
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

// --- Thread Operations ---
AngaraObject angara_spawn_thread(AngaraObject closure, int arg_count, AngaraObject args[]) {
    ThreadStartData* start_data = (ThreadStartData*)malloc(sizeof(ThreadStartData));
    if (!start_data) return angara_create_nil();

    start_data->closure = closure;
    start_data->arg_count = arg_count + 1; // +1 for the thread object itself
    angara_incref(closure);

    start_data->args = (AngaraObject*)malloc(sizeof(AngaraObject) * start_data->arg_count);
    if (!start_data->args) {
        free(start_data);
        return angara_create_nil();
    }

    AngaraThread* thread_obj = (AngaraThread*)malloc(sizeof(AngaraThread));
    thread_obj->obj.type = OBJ_THREAD;
    thread_obj->obj.ref_count = 1;
    thread_obj->return_value = angara_create_nil();
    AngaraObject thread_angara_obj = {VAL_OBJ, {.obj = (Object*)thread_obj}};

    // The first argument is the thread object itself
    start_data->args[0] = thread_angara_obj;
    angara_incref(thread_angara_obj);

    // Copy the rest of the arguments
    for (int i = 0; i < arg_count; ++i) {
        start_data->args[i + 1] = args[i];
        angara_incref(args[i]);
    }

    if (pthread_create(&thread_obj->handle, NULL, &thread_starter_routine, start_data) != 0) {
        angara_decref(start_data->closure);
        for (int i = 0; i < start_data->arg_count; ++i) angara_decref(start_data->args[i]);
        free(start_data->args);
        free(start_data);
        free(thread_obj);
        angara_throw_error("Failed to create new thread.");
        return angara_create_nil();
    }

    return thread_angara_obj;
}

AngaraObject angara_thread_join(AngaraObject thread_obj) {
    if (!IS_OBJ(thread_obj) || OBJ_TYPE(thread_obj) != OBJ_THREAD) {
        return angara_create_nil();
    }
    AngaraThread* thread = AS_THREAD(thread_obj);

    pthread_join(thread->handle, NULL);

    angara_incref(thread->return_value);
    return thread->return_value;
}

// --- Mutex Operations ---
AngaraObject angara_mutex_new(void) {
    AngaraMutex* mutex = (AngaraMutex*)malloc(sizeof(AngaraMutex));
    mutex->obj.type = OBJ_MUTEX;
    mutex->obj.ref_count = 1;

    if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
        fprintf(stderr, "Error: Failed to initialize mutex.\n");
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
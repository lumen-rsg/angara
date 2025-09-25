//
// Created by cv2 on 9/14/25.
//

#include "../runtime/angara_runtime.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

// Helper to convert a struct timespec to a double
static double timespec_to_double(struct timespec ts) {
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

// --- Stopwatch Class Implementation ---

// The private C data for a Stopwatch instance.
typedef struct {
    struct timespec start_time;
} StopwatchData;

void finalize_stopwatch(void* data) {
    free(data);
}

static inline StopwatchData* get_data(AngaraObject self) {
    return (StopwatchData*)AS_NATIVE_INSTANCE(self)->data;
}

// CONSTRUCTOR: `time.Stopwatch()`
AngaraObject Angara_time_Stopwatch(int arg_count, AngaraObject* args) {
    StopwatchData* data = (StopwatchData*)malloc(sizeof(StopwatchData));
    // CLOCK_MONOTONIC is essential for measuring intervals, as it's not
    // affected by system time changes (e.g., NTP updates).
    clock_gettime(CLOCK_MONOTONIC, &data->start_time);
    return angara_create_native_instance(data, finalize_stopwatch);
}

// METHOD: `sw.elapsed() -> f64`
AngaraObject Angara_Stopwatch_elapsed(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    StopwatchData* data = get_data(self);
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    double start = timespec_to_double(data->start_time);
    double now = timespec_to_double(current_time);

    return angara_create_f64(now - start);
}

// METHOD: `sw.reset()`
AngaraObject Angara_Stopwatch_reset(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    StopwatchData* data = get_data(self);
    clock_gettime(CLOCK_MONOTONIC, &data->start_time);
    return angara_create_nil();
}

// --- Global Function Implementations ---

// `time.now() -> f64`
AngaraObject Angara_time_now(int arg_count, AngaraObject* args) {
    struct timespec ts;
    // CLOCK_REALTIME gives the actual wall-clock time.
    clock_gettime(CLOCK_REALTIME, &ts);
    return angara_create_f64(timespec_to_double(ts));
}

// `time.sleep(seconds as f64)`
AngaraObject Angara_time_sleep(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_F64(args[0])) {
        angara_throw_error("sleep(seconds) expects one float argument.");
        return angara_create_nil();
    }
    double seconds = AS_F64(args[0]);
    if (seconds < 0) seconds = 0;

    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - req.tv_sec) * 1e9);

    nanosleep(&req, NULL);
    return angara_create_nil();
}

// `time.format_iso(timestamp as f64) -> string`
AngaraObject Angara_time_format_iso(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_F64(args[0])) {
        angara_throw_error("format_iso(timestamp) expects one float argument.");
        return angara_create_nil();
    }
    double timestamp = AS_F64(args[0]);
    time_t seconds = (time_t)timestamp;

    char buf[sizeof("YYYY-MM-DDTHH:MM:SSZ")];
    // gmtime_r is the thread-safe version of gmtime. Formats in UTC.
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&seconds, &(struct tm){0}));

    return angara_create_string(buf);
}


// --- ABI Definition ---

static const AngaraMethodDef STOPWATCH_METHODS[] = {
        {"elapsed", (AngaraMethodFn)Angara_Stopwatch_elapsed, "->d"},
        {"reset",   (AngaraMethodFn)Angara_Stopwatch_reset,   "->n"},
        {NULL, NULL, NULL}
};

static const AngaraClassDef STOPWATCH_CLASS_DEF = { "Stopwatch", NULL, STOPWATCH_METHODS };

static const AngaraFuncDef TIME_EXPORTS[] = {
        // Global Functions
        { "now",        Angara_time_now,        "->d",    NULL },
        { "sleep",      Angara_time_sleep,      "d->n",   NULL },
        { "format_iso", Angara_time_format_iso, "d->s",   NULL },
        // Constructor for the Stopwatch class
        { "Stopwatch",  Angara_time_Stopwatch, "->Stopwatch", &STOPWATCH_CLASS_DEF },
        { NULL, NULL, NULL, NULL }
};

ANGARA_MODULE_INIT(time) {
        *def_count = (sizeof(TIME_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
        return TIME_EXPORTS;
}
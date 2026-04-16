//
// time.c — Angara time module (rewritten for 16-byte ABI + vtable)
//

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "Angara.h"

static double timespec_to_double(struct timespec ts) {
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

typedef struct { struct timespec start_time; } StopwatchData;

static void finalize_stopwatch(void* data) { free(data); }

// time.now() -> f64
AngaraObject Angara_time_now(int arg_count, AngaraObject* args) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ang_f64(timespec_to_double(ts));
}

// time.sleep(seconds: f64) -> nil
AngaraObject Angara_time_sleep(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("sleep(seconds) expects one float argument.");
        return ang_nil();
    }
    double seconds = ang_as_f64(args[0]);
    if (seconds < 0) seconds = 0;
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - req.tv_sec) * 1e9);
    nanosleep(&req, NULL);
    return ang_nil();
}

// time.format_iso(timestamp: f64) -> string
AngaraObject Angara_time_format_iso(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("format_iso(timestamp) expects one float argument.");
        return ang_nil();
    }
    time_t seconds = (time_t)ang_as_f64(args[0]);
    char buf[sizeof("YYYY-MM-DDTHH:MM:SSZ")];
    struct tm tm_buf;
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&seconds, &tm_buf));
    return ang_api->string(buf);
}

// Stopwatch constructor
AngaraObject Angara_time_Stopwatch(int arg_count, AngaraObject* args) {
    StopwatchData* data = (StopwatchData*)malloc(sizeof(StopwatchData));
    clock_gettime(CLOCK_MONOTONIC, &data->start_time);
    return ang_api->native_instance_new(data, finalize_stopwatch, "Stopwatch");
}

// sw.elapsed() -> f64
AngaraObject Angara_Stopwatch_elapsed(int arg_count, AngaraObject* args) {
    StopwatchData* data = (StopwatchData*)ang_api->native_instance_data(args[0]);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return ang_f64(timespec_to_double(now) - timespec_to_double(data->start_time));
}

// sw.reset() -> nil
AngaraObject Angara_Stopwatch_reset(int arg_count, AngaraObject* args) {
    StopwatchData* data = (StopwatchData*)ang_api->native_instance_data(args[0]);
    clock_gettime(CLOCK_MONOTONIC, &data->start_time);
    return ang_nil();
}

static const AngaraMethodDef STOPWATCH_METHODS[] = {
    {"elapsed", (AngaraMethodFn)Angara_Stopwatch_elapsed, "->d"},
    {"reset",   (AngaraMethodFn)Angara_Stopwatch_reset,   "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef STOPWATCH_CLASS_DEF = { "Stopwatch", NULL, STOPWATCH_METHODS };

static const AngaraFuncDef TIME_EXPORTS[] = {
    {"now",        Angara_time_now,        "->d",    NULL},
    {"sleep",      Angara_time_sleep,      "d->n",   NULL},
    {"format_iso", Angara_time_format_iso, "d->s",   NULL},
    {"Stopwatch",  Angara_time_Stopwatch,  "->Stopwatch", &STOPWATCH_CLASS_DEF},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(time) {
    ang_api = api;
    *def_count = (sizeof(TIME_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return TIME_EXPORTS;
}
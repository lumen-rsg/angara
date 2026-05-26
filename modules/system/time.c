/// Angara time module — time.now(), epoch, formatted strings, stopwatch, sleep. Links -lrt on Linux.
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include "Angara.h"

static double timespec_to_double(struct timespec ts) {
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

typedef struct { struct timespec start_time; } StopwatchData;

static void finalize_stopwatch(void* data) { free(data); }

AngaraObject Angara_time_now(int arg_count, AngaraObject* args) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ang_f64(timespec_to_double(ts));
}

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

AngaraObject Angara_time_Stopwatch(int arg_count, AngaraObject* args) {
    StopwatchData* data = (StopwatchData*)malloc(sizeof(StopwatchData));
    clock_gettime(CLOCK_MONOTONIC, &data->start_time);
    return ang_api->native_instance_new(data, finalize_stopwatch, "Stopwatch");
}

AngaraObject Angara_Stopwatch_elapsed(int arg_count, AngaraObject* args) {
    StopwatchData* data = (StopwatchData*)ang_api->native_instance_data(args[0]);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return ang_f64(timespec_to_double(now) - timespec_to_double(data->start_time));
}

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

AngaraObject Angara_time_unix(int arg_count, AngaraObject* args) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ang_i64((int64_t)ts.tv_sec);
}

AngaraObject Angara_time_from_unix(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("from_unix(seconds) expects one i64 argument.");
        return ang_nil();
    }
    return ang_f64((double)ang_as_i64(args[0]));
}

AngaraObject Angara_time_format(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_f64(args[0]) || !ang_is_obj(args[1]) ||
        ang_api->obj_type(args[1]) != ANG_OBJ_STRING) {
        ang_api->throw_error("format(timestamp, fmt) expects a float and a string.");
        return ang_nil();
    }
    time_t seconds = (time_t)ang_as_f64(args[0]);
    struct tm tm_buf;
    gmtime_r(&seconds, &tm_buf);
    char buf[256];
    strftime(buf, sizeof(buf), ang_api->as_cstr(args[1]), &tm_buf);
    return ang_api->string(buf);
}

AngaraObject Angara_time_parse(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_obj(args[0]) || !ang_is_obj(args[1]) ||
        ang_api->obj_type(args[0]) != ANG_OBJ_STRING || ang_api->obj_type(args[1]) != ANG_OBJ_STRING) {
        ang_api->throw_error("parse(time_str, fmt) expects two strings.");
        return ang_nil();
    }
    struct tm tm_buf;
    memset(&tm_buf, 0, sizeof(tm_buf));
    char* result = strptime(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1]), &tm_buf);
    if (!result) return ang_nil();
    time_t t = timegm(&tm_buf);
    return ang_f64((double)t);
}

AngaraObject Angara_time_date_parts(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("date_parts(timestamp) expects one float argument.");
        return ang_nil();
    }
    time_t seconds = (time_t)ang_as_f64(args[0]);
    struct tm tm_buf;
    gmtime_r(&seconds, &tm_buf);

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "year", ang_i64(tm_buf.tm_year + 1900));
    ang_api->record_set(rec, "month", ang_i64(tm_buf.tm_mon + 1));
    ang_api->record_set(rec, "day", ang_i64(tm_buf.tm_mday));
    ang_api->record_set(rec, "hour", ang_i64(tm_buf.tm_hour));
    ang_api->record_set(rec, "minute", ang_i64(tm_buf.tm_min));
    ang_api->record_set(rec, "second", ang_i64(tm_buf.tm_sec));
    ang_api->record_set(rec, "weekday", ang_i64(tm_buf.tm_wday));
    ang_api->record_set(rec, "yday", ang_i64(tm_buf.tm_yday));
    return rec;
}

AngaraObject Angara_time_monotonic(int arg_count, AngaraObject* args) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ang_f64(timespec_to_double(ts));
}

static const AngaraFuncDef TIME_EXPORTS[] = {
    {"now",        Angara_time_now,        "->d",    NULL},
    {"unix",       Angara_time_unix,       "->i",    NULL},
    {"from_unix",  Angara_time_from_unix,  "i->d",   NULL},
    {"sleep",      Angara_time_sleep,      "d->n",   NULL},
    {"format_iso", Angara_time_format_iso, "d->s",   NULL},
    {"format",     Angara_time_format,     "ds->s",  NULL},
    {"parse",      Angara_time_parse,      "ss->d?", NULL},
    {"date_parts", Angara_time_date_parts, "d->{}",  NULL},
    {"monotonic",  Angara_time_monotonic,  "->d",    NULL},
    {"Stopwatch",  Angara_time_Stopwatch,  "->Stopwatch", &STOPWATCH_CLASS_DEF},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(time) {
    ang_api = api;
    *def_count = (sizeof(TIME_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return TIME_EXPORTS;
}
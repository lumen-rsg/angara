/// Angara time module — time.now(), epoch, formatted strings, stopwatch, sleep. Links -lrt on Linux.
// _GNU_SOURCE enables strptime() and timegm() on Linux (glibc). On macOS
// these are in <time.h> by default, so the define is harmless there.
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

#ifdef _WIN32
// Windows: use QueryPerformanceCounter for high-resolution time.
// MinGW-w64 provides clock_gettime via winpthread; MSVC does not.
// We provide a portable wrapper using the performance counter.
static double query_perf_counter_seconds(void) {
    static LARGE_INTEGER freq = {0};
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}
#else
static double timespec_to_double(struct timespec ts) {
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}
#endif

#ifdef _WIN32
typedef struct { double start_time; } StopwatchData;
#else
typedef struct { struct timespec start_time; } StopwatchData;
#endif

static void finalize_stopwatch(void* data) { free(data); }

AngaraObject Angara_time_now(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_f64(query_perf_counter_seconds());
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ang_f64(timespec_to_double(ts));
#endif
}

AngaraObject Angara_time_sleep(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("sleep(seconds) expects one float argument.");
        return ang_nil();
    }
    double seconds = ang_as_f64(args[0]);
    if (seconds < 0) seconds = 0;
#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000.0));
#else
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - req.tv_sec) * 1e9);
    nanosleep(&req, NULL);
#endif
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
#ifdef _WIN32
    gmtime_s(&tm_buf, &seconds);
#else
    gmtime_r(&seconds, &tm_buf);
#endif
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return ang_api->string(buf);
}

AngaraObject Angara_time_Stopwatch(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    StopwatchData* data = (StopwatchData*)malloc(sizeof(StopwatchData));
#ifdef _WIN32
    data->start_time = query_perf_counter_seconds();
#else
    clock_gettime(CLOCK_MONOTONIC, &data->start_time);
#endif
    return ang_api->native_instance_new(data, finalize_stopwatch, "Stopwatch");
}

AngaraObject Angara_Stopwatch_elapsed(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Stopwatch.elapsed: expected 1 argument"); return ang_nil(); }
    StopwatchData* data = (StopwatchData*)ang_api->native_instance_data(args[0]);
#ifdef _WIN32
    double now = query_perf_counter_seconds();
    return ang_f64(now - data->start_time);
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return ang_f64(timespec_to_double(now) - timespec_to_double(data->start_time));
#endif
}

AngaraObject Angara_Stopwatch_reset(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Stopwatch.reset: expected 1 argument"); return ang_nil(); }
    StopwatchData* data = (StopwatchData*)ang_api->native_instance_data(args[0]);
#ifdef _WIN32
    data->start_time = query_perf_counter_seconds();
#else
    clock_gettime(CLOCK_MONOTONIC, &data->start_time);
#endif
    return ang_nil();
}

static const AngaraMethodDef STOPWATCH_METHODS[] = {
    {"elapsed", (AngaraMethodFn)Angara_Stopwatch_elapsed, "->d"},
    {"reset",   (AngaraMethodFn)Angara_Stopwatch_reset,   "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef STOPWATCH_CLASS_DEF = { "Stopwatch", NULL, STOPWATCH_METHODS };

AngaraObject Angara_time_unix(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_i64((int64_t)time(NULL));
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ang_i64((int64_t)ts.tv_sec);
#endif
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
#ifdef _WIN32
    gmtime_s(&tm_buf, &seconds);
#else
    gmtime_r(&seconds, &tm_buf);
#endif
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
#ifdef _WIN32
    // Windows: strptime is not standard; use sscanf as fallback for common formats.
    // For full strptime support, link with a portability library.
    char* result = strptime(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1]), &tm_buf);
#else
    char* result = strptime(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1]), &tm_buf);
#endif
    if (!result) return ang_nil();
#ifdef _WIN32
    time_t t = _mkgmtime64(&tm_buf);
#else
    time_t t = timegm(&tm_buf);
#endif
    return ang_f64((double)t);
}

AngaraObject Angara_time_date_parts(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("date_parts(timestamp) expects one float argument.");
        return ang_nil();
    }
    time_t seconds = (time_t)ang_as_f64(args[0]);
    struct tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &seconds);
#else
    gmtime_r(&seconds, &tm_buf);
#endif

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
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_f64(query_perf_counter_seconds());
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ang_f64(timespec_to_double(ts));
#endif
}

/* ---- local-time / timezone functions ---- */

AngaraObject Angara_time_now_local(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_f64(query_perf_counter_seconds());
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ang_f64(timespec_to_double(ts));  /* same epoch; local interpretation is in formatting */
#endif
}

AngaraObject Angara_time_format_local(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_f64(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("format_local(timestamp, fmt) expects a float and a string.");
        return ang_nil();
    }
    time_t seconds = (time_t)ang_as_f64(args[0]);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &seconds);
#else
    localtime_r(&seconds, &tm_buf);
#endif
    char buf[256];
    strftime(buf, sizeof(buf), ang_api->as_cstr(args[1]), &tm_buf);
    return ang_api->string(buf);
}

AngaraObject Angara_time_date_parts_local(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("date_parts_local(timestamp) expects one float argument.");
        return ang_nil();
    }
    time_t seconds = (time_t)ang_as_f64(args[0]);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &seconds);
#else
    localtime_r(&seconds, &tm_buf);
#endif

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "year",    ang_i64(tm_buf.tm_year + 1900));
    ang_api->record_set(rec, "month",   ang_i64(tm_buf.tm_mon + 1));
    ang_api->record_set(rec, "day",     ang_i64(tm_buf.tm_mday));
    ang_api->record_set(rec, "hour",    ang_i64(tm_buf.tm_hour));
    ang_api->record_set(rec, "minute",  ang_i64(tm_buf.tm_min));
    ang_api->record_set(rec, "second",  ang_i64(tm_buf.tm_sec));
    ang_api->record_set(rec, "weekday", ang_i64(tm_buf.tm_wday));
    ang_api->record_set(rec, "yday",    ang_i64(tm_buf.tm_yday));
    ang_api->record_set(rec, "isdst",   ang_i64(tm_buf.tm_isdst));
    return rec;
}

AngaraObject Angara_time_timezone_offset(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    time_t now = time(NULL);
    struct tm local_buf, gmt_buf;
#ifdef _WIN32
    localtime_s(&local_buf, &now);
    gmtime_s(&gmt_buf, &now);
#else
    localtime_r(&now, &local_buf);
    gmtime_r(&now, &gmt_buf);
#endif
    /* tm_gmtoff is a glibc/BSD extension — use mktime/gmtime diff for portability */
    time_t local_t = mktime(&local_buf);
    /* mktime modified local_buf, so re-grab gmt */
    struct tm gmt2_buf;
#ifdef _WIN32
    gmtime_s(&gmt2_buf, &now);
    time_t gmt_t = _mkgmtime64(&gmt2_buf);
#else
    gmtime_r(&now, &gmt2_buf);
    time_t gmt_t = timegm(&gmt2_buf);
#endif
    return ang_i64((int64_t)(local_t - gmt_t));
}

AngaraObject Angara_time_timezone_name(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
#if defined(__linux__) || defined(__GLIBC__)
    return ang_api->string(tm_buf.tm_zone ? tm_buf.tm_zone : "UTC");
#else
    /* BSD/macOS/Windows: use tzname[] */
    return ang_api->string(tzname[tm_buf.tm_isdst > 0 ? 1 : 0]);
#endif
}

static const AngaraFuncDef TIME_EXPORTS[] = {
    {"now",              Angara_time_now,              "->d",    NULL},
    {"now_local",        Angara_time_now_local,        "->d",    NULL},
    {"unix",             Angara_time_unix,             "->i",    NULL},
    {"from_unix",        Angara_time_from_unix,        "i->d",   NULL},
    {"sleep",            Angara_time_sleep,            "d->n",   NULL},
    {"format_iso",       Angara_time_format_iso,       "d->s",   NULL},
    {"format",           Angara_time_format,           "ds->s",  NULL},
    {"format_local",     Angara_time_format_local,     "ds->s",  NULL},
    {"parse",            Angara_time_parse,            "ss->d?", NULL},
    {"date_parts",       Angara_time_date_parts,       "d->{}",  NULL},
    {"date_parts_local", Angara_time_date_parts_local, "d->{}",  NULL},
    {"monotonic",        Angara_time_monotonic,        "->d",    NULL},
    {"timezone_offset",  Angara_time_timezone_offset,  "->i",    NULL},
    {"timezone_name",    Angara_time_timezone_name,    "->s",    NULL},
    {"Stopwatch",        Angara_time_Stopwatch,        "->Stopwatch", &STOPWATCH_CLASS_DEF},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(time) {
    ang_api = api;
    *def_count = (sizeof(TIME_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return TIME_EXPORTS;
}
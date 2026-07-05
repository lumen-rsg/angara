/// Angara logging module — leveled logging to stderr with timestamps.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

typedef enum { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 } LogLevel;

static LogLevel g_min_level = LOG_INFO; /* default: suppress debug */

static const char* level_label(LogLevel lv) {
    switch (lv) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "?????";
    }
}

static void log_write(LogLevel lv, const char* msg) {
    if (lv < g_min_level) return;

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    fprintf(stderr, "[%s] %s  %s\n", ts, level_label(lv), msg);
}

/* ---- public API ---- */

AngaraObject Angara_log_set_level(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("log.set_level(level) expects a string: debug|info|warn|error.");
        return ang_nil();
    }
    const char* s = ang_api->as_cstr(args[0]);
    if      (strcmp(s, "debug") == 0) g_min_level = LOG_DEBUG;
    else if (strcmp(s, "info")  == 0) g_min_level = LOG_INFO;
    else if (strcmp(s, "warn")  == 0) g_min_level = LOG_WARN;
    else if (strcmp(s, "error") == 0) g_min_level = LOG_ERROR;
    else {
        ang_api->throw_error("log.set_level: unknown level; use debug|info|warn|error.");
    }
    return ang_nil();
}

AngaraObject Angara_log_debug(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) return ang_nil();
    log_write(LOG_DEBUG, ang_api->as_cstr(args[0]));
    return ang_nil();
}

AngaraObject Angara_log_info(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) return ang_nil();
    log_write(LOG_INFO, ang_api->as_cstr(args[0]));
    return ang_nil();
}

AngaraObject Angara_log_warn(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) return ang_nil();
    log_write(LOG_WARN, ang_api->as_cstr(args[0]));
    return ang_nil();
}

AngaraObject Angara_log_error(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) return ang_nil();
    log_write(LOG_ERROR, ang_api->as_cstr(args[0]));
    return ang_nil();
}


static const AngaraFuncDef LOG_EXPORTS[] = {
    {"set_level", Angara_log_set_level, "s->n", NULL},
    {"debug",     Angara_log_debug,     "s->n", NULL},
    {"info",      Angara_log_info,      "s->n", NULL},
    {"warn",      Angara_log_warn,      "s->n", NULL},
    {"error",     Angara_log_error,     "s->n", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(log) {
    ang_api = api;
    *def_count = (sizeof(LOG_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return LOG_EXPORTS;
}

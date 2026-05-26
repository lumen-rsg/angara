/// Angara color module — ANSI color and formatting code generation.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

static AngaraObject ansi_code(const char* code) {
    return ang_api->string(code);
}

AngaraObject Angara_color_black(int ac, AngaraObject* a)   { return ansi_code("\033[30m"); }
AngaraObject Angara_color_red(int ac, AngaraObject* a)     { return ansi_code("\033[31m"); }
AngaraObject Angara_color_green(int ac, AngaraObject* a)   { return ansi_code("\033[32m"); }
AngaraObject Angara_color_yellow(int ac, AngaraObject* a)  { return ansi_code("\033[33m"); }
AngaraObject Angara_color_blue(int ac, AngaraObject* a)    { return ansi_code("\033[34m"); }
AngaraObject Angara_color_magenta(int ac, AngaraObject* a) { return ansi_code("\033[35m"); }
AngaraObject Angara_color_cyan(int ac, AngaraObject* a)    { return ansi_code("\033[36m"); }
AngaraObject Angara_color_white(int ac, AngaraObject* a)   { return ansi_code("\033[37m"); }

AngaraObject Angara_color_bg_black(int ac, AngaraObject* a)   { return ansi_code("\033[40m"); }
AngaraObject Angara_color_bg_red(int ac, AngaraObject* a)     { return ansi_code("\033[41m"); }
AngaraObject Angara_color_bg_green(int ac, AngaraObject* a)   { return ansi_code("\033[42m"); }
AngaraObject Angara_color_bg_yellow(int ac, AngaraObject* a)  { return ansi_code("\033[43m"); }
AngaraObject Angara_color_bg_blue(int ac, AngaraObject* a)    { return ansi_code("\033[44m"); }
AngaraObject Angara_color_bg_magenta(int ac, AngaraObject* a) { return ansi_code("\033[45m"); }
AngaraObject Angara_color_bg_cyan(int ac, AngaraObject* a)    { return ansi_code("\033[46m"); }
AngaraObject Angara_color_bg_white(int ac, AngaraObject* a)   { return ansi_code("\033[47m"); }

AngaraObject Angara_color_bright_black(int ac, AngaraObject* a)   { return ansi_code("\033[90m"); }
AngaraObject Angara_color_bright_red(int ac, AngaraObject* a)     { return ansi_code("\033[91m"); }
AngaraObject Angara_color_bright_green(int ac, AngaraObject* a)   { return ansi_code("\033[92m"); }
AngaraObject Angara_color_bright_yellow(int ac, AngaraObject* a)  { return ansi_code("\033[93m"); }
AngaraObject Angara_color_bright_blue(int ac, AngaraObject* a)    { return ansi_code("\033[94m"); }
AngaraObject Angara_color_bright_magenta(int ac, AngaraObject* a) { return ansi_code("\033[95m"); }
AngaraObject Angara_color_bright_cyan(int ac, AngaraObject* a)    { return ansi_code("\033[96m"); }
AngaraObject Angara_color_bright_white(int ac, AngaraObject* a)   { return ansi_code("\033[97m"); }

AngaraObject Angara_color_bold(int ac, AngaraObject* a)      { return ansi_code("\033[1m"); }
AngaraObject Angara_color_dim(int ac, AngaraObject* a)       { return ansi_code("\033[2m"); }
AngaraObject Angara_color_italic(int ac, AngaraObject* a)    { return ansi_code("\033[3m"); }
AngaraObject Angara_color_underline(int ac, AngaraObject* a) { return ansi_code("\033[4m"); }
AngaraObject Angara_color_blink(int ac, AngaraObject* a)     { return ansi_code("\033[5m"); }
AngaraObject Angara_color_reverse(int ac, AngaraObject* a)   { return ansi_code("\033[7m"); }
AngaraObject Angara_color_hidden(int ac, AngaraObject* a)    { return ansi_code("\033[8m"); }

AngaraObject Angara_color_reset(int ac, AngaraObject* a) { return ansi_code("\033[0m"); }

AngaraObject Angara_color_fg(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("fg(code) expects one i64 argument (0-255).");
        return ang_nil();
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "\033[38;5;%dm", (int)ang_as_i64(args[0]));
    return ang_api->string(buf);
}

AngaraObject Angara_color_bg(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("bg(code) expects one i64 argument (0-255).");
        return ang_nil();
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "\033[48;5;%dm", (int)ang_as_i64(args[0]));
    return ang_api->string(buf);
}

AngaraObject Angara_color_rgb_fg(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !ang_is_i64(args[0]) || !ang_is_i64(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("rgb_fg(r, g, b) expects three i64 arguments.");
        return ang_nil();
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm",
             (int)ang_as_i64(args[0]), (int)ang_as_i64(args[1]), (int)ang_as_i64(args[2]));
    return ang_api->string(buf);
}

AngaraObject Angara_color_rgb_bg(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !ang_is_i64(args[0]) || !ang_is_i64(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("rgb_bg(r, g, b) expects three i64 arguments.");
        return ang_nil();
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm",
             (int)ang_as_i64(args[0]), (int)ang_as_i64(args[1]), (int)ang_as_i64(args[2]));
    return ang_api->string(buf);
}

AngaraObject Angara_color_style(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("style(text, *codes) expects a string and optional color codes.");
        return ang_nil();
    }
    size_t text_len = ang_api->str_len(args[0]);
    size_t total = text_len + 4 + 4;
    for (int i = 1; i < arg_count; i++) {
        if (IS_STR(args[i])) total += ang_api->str_len(args[i]);
    }

    char* buf = (char*)malloc(total + 1);
    if (!buf) { ang_api->throw_error("color.style: out of memory."); return ang_nil(); }
    size_t pos = 0;

    for (int i = 1; i < arg_count; i++) {
        if (IS_STR(args[i])) {
            size_t l = ang_api->str_len(args[i]);
            memcpy(buf + pos, ang_api->as_cstr(args[i]), l);
            pos += l;
        }
    }
    memcpy(buf + pos, ang_api->as_cstr(args[0]), text_len);
    pos += text_len;
    memcpy(buf + pos, "\033[0m", 4);
    pos += 4;
    buf[pos] = '\0';
    return ang_api->string_no_copy(buf, pos);
}

static const AngaraFuncDef COLOR_EXPORTS[] = {
    {"black",          Angara_color_black,          "->s", NULL},
    {"red",            Angara_color_red,            "->s", NULL},
    {"green",          Angara_color_green,          "->s", NULL},
    {"yellow",         Angara_color_yellow,         "->s", NULL},
    {"blue",           Angara_color_blue,           "->s", NULL},
    {"magenta",        Angara_color_magenta,        "->s", NULL},
    {"cyan",           Angara_color_cyan,           "->s", NULL},
    {"white",          Angara_color_white,          "->s", NULL},
    {"bright_black",   Angara_color_bright_black,   "->s", NULL},
    {"bright_red",     Angara_color_bright_red,     "->s", NULL},
    {"bright_green",   Angara_color_bright_green,   "->s", NULL},
    {"bright_yellow",  Angara_color_bright_yellow,  "->s", NULL},
    {"bright_blue",    Angara_color_bright_blue,    "->s", NULL},
    {"bright_magenta", Angara_color_bright_magenta, "->s", NULL},
    {"bright_cyan",    Angara_color_bright_cyan,    "->s", NULL},
    {"bright_white",   Angara_color_bright_white,   "->s", NULL},
    {"bg_black",       Angara_color_bg_black,       "->s", NULL},
    {"bg_red",         Angara_color_bg_red,         "->s", NULL},
    {"bg_green",       Angara_color_bg_green,       "->s", NULL},
    {"bg_yellow",      Angara_color_bg_yellow,      "->s", NULL},
    {"bg_blue",        Angara_color_bg_blue,        "->s", NULL},
    {"bg_magenta",     Angara_color_bg_magenta,     "->s", NULL},
    {"bg_cyan",        Angara_color_bg_cyan,        "->s", NULL},
    {"bg_white",       Angara_color_bg_white,       "->s", NULL},
    {"bold",           Angara_color_bold,           "->s", NULL},
    {"dim",            Angara_color_dim,            "->s", NULL},
    {"italic",         Angara_color_italic,         "->s", NULL},
    {"underline",      Angara_color_underline,      "->s", NULL},
    {"blink",          Angara_color_blink,          "->s", NULL},
    {"reverse",        Angara_color_reverse,        "->s", NULL},
    {"hidden",         Angara_color_hidden,         "->s", NULL},
    {"reset",          Angara_color_reset,          "->s", NULL},
    {"fg",             Angara_color_fg,             "i->s", NULL},
    {"bg",             Angara_color_bg,             "i->s", NULL},
    {"rgb_fg",         Angara_color_rgb_fg,         "iii->s", NULL},
    {"rgb_bg",         Angara_color_rgb_bg,         "iii->s", NULL},
    {"style",          Angara_color_style,          "s*s->s", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(color) {
    ang_api = api;
    *def_count = (sizeof(COLOR_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return COLOR_EXPORTS;
}
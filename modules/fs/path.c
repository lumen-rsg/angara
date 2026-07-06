/// Angara path module — path join, split, basename, dirname, extension manipulation.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define SEP '/'

AngaraObject Angara_path_join(int arg_count, AngaraObject* args) {
    if (arg_count == 0) return ang_api->string(".");

    size_t total_len = 0;
    for (int i = 0; i < arg_count; i++) {
        if (!IS_STR(args[i])) { ang_api->throw_error("path.join() arguments must be strings."); return ang_nil(); }
        total_len += ang_api->str_len(args[i]);
    }
    total_len += (arg_count - 1) + 1;

    char* buf = (char*)malloc(total_len);
    if (!buf) { ang_api->throw_error("Out of memory in path.join()."); return ang_nil(); }

    char* ptr = buf;
    for (int i = 0; i < arg_count; i++) {
        const char* part = ang_api->as_cstr(args[i]);
        size_t len = ang_api->str_len(args[i]);
        if (i > 0) *ptr++ = SEP;
        memcpy(ptr, part, len);
        ptr += len;
    }
    *ptr = '\0';

    return ang_api->string_no_copy(buf, ptr - buf);
}

AngaraObject Angara_path_basename(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("basename(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    const char* last = strrchr(path, SEP);
    if (!last) return ang_api->string(path);
    return ang_api->string(last + 1);
}

AngaraObject Angara_path_dirname(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("dirname(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    const char* last = strrchr(path, SEP);
    if (!last) return ang_api->string(".");
    if (last == path) return ang_api->string("/");
    size_t len = last - path;
    char* buf = (char*)malloc(len + 1);
    memcpy(buf, path, len);
    buf[len] = '\0';
    return ang_api->string_no_copy(buf, len);
}

AngaraObject Angara_path_extension(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("extension(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, SEP);
    if (!dot || (slash && slash > dot)) return ang_api->string("");
    return ang_api->string(dot);
}

AngaraObject Angara_path_stem(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("stem(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    const char* slash = strrchr(path, SEP);
    const char* base = slash ? slash + 1 : path;
    const char* dot = strrchr(base, '.');
    if (!dot || dot == base) return ang_api->string(base);
    size_t len = dot - base;
    char* buf = (char*)malloc(len + 1);
    memcpy(buf, base, len); buf[len] = '\0';
    return ang_api->string_no_copy(buf, len);
}

AngaraObject Angara_path_is_absolute(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_absolute(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    return ang_bool(path[0] == SEP);
}

AngaraObject Angara_path_normalize(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("normalize(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    if (len == 0) return ang_api->string(".");

    char* tmp = (char*)malloc(len + 1);
    memcpy(tmp, path, len + 1);

    char* parts[256];
    int count = 0;
    int is_abs = (path[0] == SEP);

    char* tok = strtok(tmp, "/");
    while (tok && count < 255) {
        if (strcmp(tok, ".") == 0) { }
        else if (strcmp(tok, "..") == 0) { if (count > 0) count--; else if (!is_abs) { parts[count++] = ".."; } }
        else if (tok[0] != '\0') { parts[count++] = tok; }
        tok = strtok(NULL, "/");
    }
    free(tmp);

    if (count == 0) return ang_api->string(is_abs ? "/" : ".");

    size_t out_len = is_abs ? 1 : 0;
    for (int i = 0; i < count; i++) out_len += strlen(parts[i]) + 1;

    char* buf = (char*)malloc(out_len + 1);
    char* ptr = buf;
    if (is_abs) *ptr++ = SEP;
    for (int i = 0; i < count; i++) {
        size_t plen = strlen(parts[i]);
        memcpy(ptr, parts[i], plen); ptr += plen;
        if (i < count - 1) *ptr++ = SEP;
    }
    *ptr = '\0';
    return ang_api->string_no_copy(buf, ptr - buf);
}

AngaraObject Angara_path_separator(int arg_count, AngaraObject* args) {
    return ang_api->string("/");
}

AngaraObject Angara_path_parent(int arg_count, AngaraObject* args) {
    return Angara_path_dirname(arg_count, args);
}

AngaraObject Angara_path_with_extension(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("path.with_extension: expected 2 arguments"); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    size_t path_len = ang_api->str_len(args[0]);
    const char* ext = ang_api->as_cstr(args[1]);
    size_t ext_len = ang_api->str_len(args[1]);

    const char* slash = strrchr(path, SEP);
    const char* base = slash ? slash + 1 : path;
    const char* dot = strrchr(base, '.');

    size_t prefix_len;
    if (dot && dot != base) prefix_len = dot - path;
    else prefix_len = path_len;

    size_t out_len = prefix_len + 1 + ext_len;
    char* buf = (char*)malloc(out_len + 1);
    memcpy(buf, path, prefix_len);
    buf[prefix_len] = '.';
    memcpy(buf + prefix_len + 1, ext, ext_len);
    buf[out_len] = '\0';
    return ang_api->string_no_copy(buf, out_len);
}

static const AngaraFuncDef PATH_EXPORTS[] = {
    {"join",           Angara_path_join,           "s...->s", NULL},
    {"basename",       Angara_path_basename,       "s->s",    NULL},
    {"dirname",        Angara_path_dirname,        "s->s",    NULL},
    {"extension",      Angara_path_extension,      "s->s",    NULL},
    {"stem",           Angara_path_stem,           "s->s",    NULL},
    {"is_absolute",    Angara_path_is_absolute,    "s->b",    NULL},
    {"normalize",      Angara_path_normalize,      "s->s",    NULL},
    {"separator",      Angara_path_separator,      "->s",     NULL},
    {"parent",         Angara_path_parent,         "s->s",    NULL},
    {"with_extension", Angara_path_with_extension, "ss->s",   NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(path) {
    ang_api = api;
    *def_count = (sizeof(PATH_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return PATH_EXPORTS;
}
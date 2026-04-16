//
// path.c — Angara path manipulation module (rewritten for 16-byte ABI + vtable)
//

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define SEP '/'

// path.join(components...) -> string
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

// path.basename(path) -> string
AngaraObject Angara_path_basename(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("basename(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    const char* last = strrchr(path, SEP);
    if (!last) return ang_api->string(path);
    return ang_api->string(last + 1);
}

// path.dirname(path) -> string
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

// path.extension(path) -> string
AngaraObject Angara_path_extension(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("extension(path) expects one string."); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, SEP);
    if (!dot || (slash && slash > dot)) return ang_api->string("");
    return ang_api->string(dot);
}

static const AngaraFuncDef PATH_EXPORTS[] = {
    {"join",      Angara_path_join,      "s...->s", NULL},
    {"basename",  Angara_path_basename,  "s->s",    NULL},
    {"dirname",   Angara_path_dirname,   "s->s",    NULL},
    {"extension", Angara_path_extension, "s->s",    NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(path) {
    ang_api = api;
    *def_count = (sizeof(PATH_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return PATH_EXPORTS;
}
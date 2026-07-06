/// Angara args module — CLI argument parsing with flags, options, and positional arguments.
#include <stdlib.h>
#include <string.h>
#include "Angara.h"

#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)
#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

static int starts_with(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void rec_set_str(AngaraObject rec, const char* key, const char* val) {
    ang_api->record_set(rec, key, ang_api->string(val));
}

static void rec_set_bool(AngaraObject rec, const char* key, int val) {
    ang_api->record_set(rec, key, ang_bool(val));
}

static void list_push_str(AngaraObject list, const char* val) {
    ang_api->list_push(list, ang_api->string(val));
}

AngaraObject Angara_args_parse(int arg_count, AngaraObject* args) {

    AngaraObject argv = args[0];
    size_t argv_len = ang_api->list_len(argv);

    AngaraObject spec = arg_count >= 2 && ang_is_obj(args[1]) && ang_api->obj_type(args[1]) == ANG_OBJ_RECORD
                        ? args[1] : ang_nil();


    AngaraObject positional = ang_api->list_new();
    AngaraObject flags = ang_api->record_new();
    AngaraObject options = ang_api->record_new();

    int past_separator = 0;

    for (size_t i = 0; i < argv_len; i++) {
        AngaraObject elem = ang_api->list_get(argv, (int64_t)i);
        if (!IS_STR(elem)) {
            ang_api->list_push(positional, elem);
            ang_api->decref(elem);
            continue;
        }

        const char* arg = ang_api->as_cstr(elem);
        size_t arg_len = ang_api->str_len(elem);
        ang_api->decref(elem);

        if (past_separator) {
            list_push_str(positional, arg);
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            past_separator = 1;
            continue;
        }

        if (starts_with(arg, "--")) {
            const char* rest = arg + 2;
            const char* eq = strchr(rest, '=');
            if (eq) {
                size_t key_len = (size_t)(eq - rest);
                char* key = (char*)malloc(key_len + 1);
                memcpy(key, rest, key_len);
                key[key_len] = '\0';
                rec_set_str(options, key, eq + 1);
                free(key);
            } else {
                AngaraObject next = (i + 1 < argv_len) ? ang_api->list_get(argv, (int64_t)(i + 1)) : ang_nil();
                const char* next_str = IS_STR(next) ? ang_api->as_cstr(next) : NULL;
                int next_is_value = next_str && next_str[0] != '-';

                if (next_is_value) {
                    rec_set_str(options, rest, next_str);
                    i++;
                    ang_api->decref(next);
                } else {
                    rec_set_bool(flags, rest, 1);
                    if (!ang_is_nil(next)) ang_api->decref(next);
                }
            }
            continue;
        }

        if (arg_len >= 2 && arg[0] == '-' && arg[1] != '-') {
            if (arg_len > 2) {

                if (arg_len == 3) {
                }

                for (size_t c = 1; c < arg_len; c++) {
                    char key[2] = {arg[c], '\0'};

                    if (c == arg_len - 1 && arg_len == 2) {
                        AngaraObject next = (i + 1 < argv_len) ? ang_api->list_get(argv, (int64_t)(i + 1)) : ang_nil();
                        const char* next_str = IS_STR(next) ? ang_api->as_cstr(next) : NULL;
                        int next_is_value = next_str && next_str[0] != '-';

                        if (next_is_value) {
                            rec_set_str(options, key, next_str);
                            i++;
                            ang_api->decref(next);
                            break;
                        }
                        if (!ang_is_nil(next)) ang_api->decref(next);
                    }

                    rec_set_bool(flags, key, 1);
                }
            } else {
                char key[2] = {arg[1], '\0'};
                AngaraObject next = (i + 1 < argv_len) ? ang_api->list_get(argv, (int64_t)(i + 1)) : ang_nil();
                const char* next_str = IS_STR(next) ? ang_api->as_cstr(next) : NULL;
                int next_is_value = next_str && next_str[0] != '-';

                if (next_is_value) {
                    rec_set_str(options, key, next_str);
                    i++;
                    ang_api->decref(next);
                } else {
                    rec_set_bool(flags, key, 1);
                    if (!ang_is_nil(next)) ang_api->decref(next);
                }
            }
            continue;
        }

        list_push_str(positional, arg);
    }

    AngaraObject result = ang_api->record_new();
    ang_api->record_set(result, "args", positional);
    ang_api->record_set(result, "flags", flags);
    ang_api->record_set(result, "options", options);
    ang_api->decref(positional);
    ang_api->decref(flags);
    ang_api->decref(options);

    return result;
}

AngaraObject Angara_args_flag(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_RECORD || !IS_STR(args[1])) {
        ang_api->throw_error("flag(parsed, name) expects a parsed record and a string name.");
        return ang_nil();
    }
    AngaraObject flags_rec = ang_api->record_get(args[0], "flags");
    if (ang_is_nil(flags_rec)) return ang_bool(false);
    AngaraObject val = ang_api->record_get(flags_rec, ang_api->as_cstr(args[1]));
    int result = ang_is_bool(val) && ang_as_bool(val);
    ang_api->decref(flags_rec);
    ang_api->decref(val);
    return ang_bool(result);
}

AngaraObject Angara_args_option(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_RECORD || !IS_STR(args[1])) {
        ang_api->throw_error("option(parsed, name, default?) expects a parsed record, a string name, and optional default.");
        return ang_nil();
    }
    AngaraObject opts_rec = ang_api->record_get(args[0], "options");
    if (ang_is_nil(opts_rec)) {
        return arg_count >= 3 ? args[2] : ang_nil();
    }
    AngaraObject val = ang_api->record_get(opts_rec, ang_api->as_cstr(args[1]));
    ang_api->decref(opts_rec);
    if (ang_is_nil(val)) {
        return arg_count >= 3 ? args[2] : ang_nil();
    }
    return val;
}

AngaraObject Angara_args_positional(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_RECORD || !ang_is_i64(args[1])) {
        ang_api->throw_error("positional(parsed, index, default?) expects a parsed record and an index.");
        return ang_nil();
    }
    AngaraObject args_list = ang_api->record_get(args[0], "args");
    if (ang_is_nil(args_list)) {
        return arg_count >= 3 ? args[2] : ang_nil();
    }
    int64_t idx = ang_as_i64(args[1]);
    if (idx < 0 || (size_t)idx >= ang_api->list_len(args_list)) {
        ang_api->decref(args_list);
        return arg_count >= 3 ? args[2] : ang_nil();
    }
    AngaraObject val = ang_api->list_get(args_list, idx);
    ang_api->decref(args_list);
    return val;
}


static const AngaraFuncDef ARGS_EXPORTS[] = {
    {"parse",       Angara_args_parse,       "l{}?->{}",    NULL},
    {"flag",        Angara_args_flag,        "{}s->b",      NULL},
    {"option",      Angara_args_option,      "{}ss?->s",    NULL},
    {"positional",  Angara_args_positional,  "{}is?->s",    NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(args) {
    ang_api = api;
    *def_count = (sizeof(ARGS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ARGS_EXPORTS;
}
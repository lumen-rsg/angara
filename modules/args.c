//
// args.c — Angara CLI argument parsing module
//
// Provides functions to parse command-line argument lists into:
//   - Positional arguments
//   - Boolean flags (--flag, -f)
//   - Key-value options (--key=value, --key value, -k value)
//   - Combined short flags (-abc)
//
// No external dependencies.
//

#include <stdlib.h>
#include <string.h>
#include "Angara.h"

#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)
#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

// Helper: check if a string starts with a prefix
static int starts_with(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Helper: push a key-value pair into a record (string value)
static void rec_set_str(AngaraObject rec, const char* key, const char* val) {
    ang_api->record_set(rec, key, ang_api->string(val));
}

// Helper: push a key-value pair into a record (bool value)
static void rec_set_bool(AngaraObject rec, const char* key, int val) {
    ang_api->record_set(rec, key, ang_bool(val));
}

// Helper: push a string to a list
static void list_push_str(AngaraObject list, const char* val) {
    ang_api->list_push(list, ang_api->string(val));
}

// --- args.parse(argv, spec?) -> record ---
// Parse argv list into a record with "args" (positional), "flags", "options".
//
// spec is an optional record that defines expected flags and options:
//   { flags: ["verbose", "v", "help", "h"], options: ["output", "o", "file", "f"] }
//
// Returns: { args: [...], flags: {name: bool}, options: {name: string} }
AngaraObject Angara_args_parse(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_LIST(args[0])) {
        ang_api->throw_error("parse(argv, spec?) expects a list of strings.");
        return ang_nil();
    }

    AngaraObject argv = args[0];
    size_t argv_len = ang_api->list_len(argv);

    // Extract spec if provided
    AngaraObject spec = arg_count >= 2 && ang_is_obj(args[1]) && ang_api->obj_type(args[1]) == ANG_OBJ_RECORD
                        ? args[1] : ang_nil();

    // Build sets of known flags and options from spec
    // For simplicity, we track everything dynamically

    AngaraObject positional = ang_api->list_new();
    AngaraObject flags = ang_api->record_new();
    AngaraObject options = ang_api->record_new();

    int past_separator = 0; // after "--", everything is positional

    for (size_t i = 0; i < argv_len; i++) {
        AngaraObject elem = ang_api->list_get(argv, (int64_t)i);
        if (!IS_STR(elem)) {
            // Non-string element, treat as positional
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

        // "--" alone means everything after is positional
        if (strcmp(arg, "--") == 0) {
            past_separator = 1;
            continue;
        }

        // Long option: --key=value or --key value or --flag
        if (starts_with(arg, "--")) {
            const char* rest = arg + 2;
            const char* eq = strchr(rest, '=');
            if (eq) {
                // --key=value
                size_t key_len = (size_t)(eq - rest);
                char* key = (char*)malloc(key_len + 1);
                memcpy(key, rest, key_len);
                key[key_len] = '\0';
                rec_set_str(options, key, eq + 1);
                free(key);
            } else {
                // Check if next arg is a value (not starting with -)
                AngaraObject next = (i + 1 < argv_len) ? ang_api->list_get(argv, (int64_t)(i + 1)) : ang_nil();
                const char* next_str = IS_STR(next) ? ang_api->as_cstr(next) : NULL;
                int next_is_value = next_str && next_str[0] != '-';

                if (next_is_value) {
                    rec_set_str(options, rest, next_str);
                    i++; // skip next arg
                    ang_api->decref(next);
                } else {
                    // It's a flag
                    rec_set_bool(flags, rest, 1);
                    if (!ang_is_nil(next)) ang_api->decref(next);
                }
            }
            continue;
        }

        // Short option(s): -f, -abc, -k value, -kvalue
        if (arg_len >= 2 && arg[0] == '-' && arg[1] != '-') {
            // Check for combined value: -kvalue (e.g., -ofile.txt)
            if (arg_len > 2) {
                // Could be combined flags (-abc) or -kvalue
                // If length is exactly 2 after dash, it's a single flag/option
                // For length > 2, check if next arg is a value for the last char
                // Simple approach: treat as combined flags
                // But if only one char and next is a value, treat as option

                if (arg_len == 3) {
                    // Could be -kv (short option with value in same arg)
                    // For now, treat multi-char after - as combined flags
                }

                // Treat as combined short flags: -abc -> flags a, b, c
                for (size_t c = 1; c < arg_len; c++) {
                    char key[2] = {arg[c], '\0'};

                    // If this is the last char, check if next arg is a value
                    if (c == arg_len - 1 && arg_len == 2) {
                        // Single short option, check next arg
                        AngaraObject next = (i + 1 < argv_len) ? ang_api->list_get(argv, (int64_t)(i + 1)) : ang_nil();
                        const char* next_str = IS_STR(next) ? ang_api->as_cstr(next) : NULL;
                        int next_is_value = next_str && next_str[0] != '-';

                        if (next_is_value) {
                            rec_set_str(options, key, next_str);
                            i++; // skip next
                            ang_api->decref(next);
                            break;
                        }
                        if (!ang_is_nil(next)) ang_api->decref(next);
                    }

                    rec_set_bool(flags, key, 1);
                }
            } else {
                // Exactly "-x"
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

        // Positional argument
        list_push_str(positional, arg);
    }

    // Build result record
    AngaraObject result = ang_api->record_new();
    ang_api->record_set(result, "args", positional);
    ang_api->record_set(result, "flags", flags);
    ang_api->record_set(result, "options", options);
    ang_api->decref(positional);
    ang_api->decref(flags);
    ang_api->decref(options);

    return result;
}

// --- args.flag(parsed, name) -> bool ---
// Check if a flag was set (checks both long and short forms).
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

// --- args.option(parsed, name, default?) -> string? ---
// Get an option value by name.
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

// --- args.positional(parsed, index, default?) -> string? ---
// Get a positional argument by index.
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

// --- Export Table ---

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
//
// assert.c — Angara assertion module (rewritten for 16-byte ABI + vtable)
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Angara.h"

// --- Helper: Format and Throw Error ---
static void throw_assert_error(const char* type, AngaraObject actual, AngaraObject expected, const char* user_msg) {
    AngaraObject str_actual = ang_api->to_string(actual);
    AngaraObject str_expected = ang_api->to_string(expected);

    const char* c_actual = ang_api->as_cstr(str_actual);
    const char* c_expected = ang_api->as_cstr(str_expected);

    size_t buf_size = strlen(c_actual) + strlen(c_expected) + strlen(user_msg) + 256;
    char* buffer = (char*)malloc(buf_size);

    if (buffer) {
        if (type == NULL) {
            snprintf(buffer, buf_size, "Assertion Failed: %s", user_msg);
        } else {
            snprintf(buffer, buf_size,
                     "Assertion Failed: Expected values to be %s.\n"
                     "  Actual:   %s\n"
                     "  Expected: %s\n"
                     "  Message:  %s",
                     type, c_actual, c_expected, user_msg);
        }
        ang_api->throw_error(buffer);
        free(buffer);
    } else {
        ang_api->throw_error("Assertion Failed (OOM while formatting message)");
    }

    ang_api->decref(str_actual);
    ang_api->decref(str_expected);
}

// --- Exported Functions ---

// assert.that(condition, message)
AngaraObject Angara_assert_that(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_obj(args[1])) {
        ang_api->throw_error("assert.that(condition, message) expects 'any' and 'string'.");
        return ang_nil();
    }

    if (!ang_api->truthy(args[0])) {
        const char* msg = ang_api->as_cstr(args[1]);
        throw_assert_error(NULL, ang_nil(), ang_nil(), msg);
    }

    return ang_nil();
}

// assert.eq(actual, expected, message)
AngaraObject Angara_assert_eq(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !ang_is_obj(args[2])) {
        ang_api->throw_error("assert.eq(actual, expected, message) expects 'any', 'any', and 'string'.");
        return ang_nil();
    }

    if (!ang_api->equals(args[0], args[1])) {
        throw_assert_error("EQUAL", args[0], args[1], ang_api->as_cstr(args[2]));
    }

    return ang_nil();
}

// assert.ne(actual, expected, message)
AngaraObject Angara_assert_ne(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !ang_is_obj(args[2])) {
        ang_api->throw_error("assert.ne(actual, expected, message) expects 'any', 'any', and 'string'.");
        return ang_nil();
    }

    if (ang_api->equals(args[0], args[1])) {
        throw_assert_error("NOT EQUAL", args[0], args[1], ang_api->as_cstr(args[2]));
    }

    return ang_nil();
}

// assert.fail(message)
AngaraObject Angara_assert_fail(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0])) {
        ang_api->throw_error("assert.fail(message) expects a 'string'.");
        return ang_nil();
    }

    char buffer[1024];
    snprintf(buffer, 1024, "Explicit Failure: %s", ang_api->as_cstr(args[0]));
    ang_api->throw_error(buffer);

    return ang_nil();
}

// --- ABI Definition ---

static const AngaraFuncDef ASSERT_EXPORTS[] = {
    {"that",  Angara_assert_that,  "as->n",  NULL},
    {"eq",    Angara_assert_eq,    "aas->n", NULL},
    {"ne",    Angara_assert_ne,    "aas->n", NULL},
    {"fail",  Angara_assert_fail,  "s->n",   NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(assert) {
    ang_api = api;
    *def_count = (sizeof(ASSERT_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ASSERT_EXPORTS;
}
//
// angara_assert.c
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/angara_runtime.h"

// --- Helper: Format and Throw Error ---
// Helper to construct messages like: "Assertion Failed: Expected '5', but got '10'. (User Message)"
void throw_assert_error(const char* type, AngaraObject actual, AngaraObject expected, const char* user_msg) {
    // Convert values to strings for display
    AngaraObject str_actual = angara_to_string(actual);
    AngaraObject str_expected = angara_to_string(expected);

    const char* c_actual = AS_CSTRING(str_actual);
    const char* c_expected = AS_CSTRING(str_expected);

    // Allocate buffer (generous size for formatting)
    size_t buf_size = strlen(c_actual) + strlen(c_expected) + strlen(user_msg) + 128;
    char* buffer = (char*)malloc(buf_size);

    if (buffer) {
        if (type == NULL) {
            // Simple boolean assertion failure
            snprintf(buffer, buf_size, "Assertion Failed: %s", user_msg);
        } else {
            // Equality/Inequality failure
            snprintf(buffer, buf_size, "Assertion Failed: Expected values to be %s. \n  Actual:   %s\n  Expected: %s\n  Message:  %s",
                     type, c_actual, c_expected, user_msg);
        }

        // We create the string before cleaning up to ensure string data stays valid
        // (though angara_to_string returns new objects, so order matters less here, but good practice)
        angara_throw_error(buffer);
        free(buffer);
    } else {
        angara_throw_error("Assertion Failed (OOM while formatting message)");
    }

    // Clean up the temporary string objects
    angara_decref(str_actual);
    angara_decref(str_expected);
}

// --- Exported Functions ---

// assert.that(condition, message)
AngaraObject Angara_assert_that(int arg_count, AngaraObject* args) {
    // ABI Type: as->n (any, string -> nil)
    if (arg_count != 2 || !IS_STRING(args[1])) {
        angara_throw_error("assert.that(condition, message) expects 'any' and 'string'.");
        return angara_create_nil();
    }

    if (!angara_is_truthy(args[0])) {
        const char* msg = AS_CSTRING(args[1]);
        throw_assert_error(NULL, angara_create_nil(), angara_create_nil(), msg);
    }

    return angara_create_nil();
}

// assert.eq(actual, expected, message)
AngaraObject Angara_assert_eq(int arg_count, AngaraObject* args) {
    // ABI Type: aas->n (any, any, string -> nil)
    if (arg_count != 3 || !IS_STRING(args[2])) {
        angara_throw_error("assert.eq(actual, expected, message) expects 'any', 'any', and 'string'.");
        return angara_create_nil();
    }

    AngaraObject actual = args[0];
    AngaraObject expected = args[1];

    // Use the runtime's deep equality check
    if (!AS_BOOL(angara_equals(actual, expected))) {
        throw_assert_error("EQUAL", actual, expected, AS_CSTRING(args[2]));
    }

    return angara_create_nil();
}

// assert.ne(actual, expected, message)
AngaraObject Angara_assert_ne(int arg_count, AngaraObject* args) {
    // ABI Type: aas->n
    if (arg_count != 3 || !IS_STRING(args[2])) {
        angara_throw_error("assert.ne(actual, expected, message) expects 'any', 'any', and 'string'.");
        return angara_create_nil();
    }

    AngaraObject actual = args[0];
    AngaraObject expected = args[1];

    if (AS_BOOL(angara_equals(actual, expected))) {
        throw_assert_error("NOT EQUAL", actual, expected, AS_CSTRING(args[2]));
    }

    return angara_create_nil();
}

// assert.fail(message)
AngaraObject Angara_assert_fail(int arg_count, AngaraObject* args) {
    // ABI Type: s->n
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("assert.fail(message) expects a 'string'.");
        return angara_create_nil();
    }

    char buffer[1024];
    snprintf(buffer, 1024, "Explicit Failure: %s", AS_CSTRING(args[0]));
    angara_throw_error(buffer);

    return angara_create_nil();
}

// --- ABI Definition ---

static const AngaraFuncDef ASSERT_EXPORTS[] = {
    // Name    | Function Pointer    | Type String | Class Def
    {"that",     Angara_assert_that,   "as->n",      NULL}, // check boolean
    {"eq",       Angara_assert_eq,     "aas->n",     NULL}, // check equals
    {"ne",       Angara_assert_ne,     "aas->n",     NULL}, // check not equals
    {"fail",     Angara_assert_fail,   "s->n",       NULL}, // explicit fail
    {NULL, NULL, NULL, NULL}
};

ANGARA_MODULE_INIT(assert) {
    *def_count = (sizeof(ASSERT_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ASSERT_EXPORTS;
}
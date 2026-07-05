/// Minimal test module for ang_api->call() verification.
#include <stdio.h>
#include <stdlib.h>
#include "Angara.h"

/* invoke(fn, a, b) — calls fn(a, b) via ang_api->call() and returns the result. */
AngaraObject Angara_calltest_invoke(int arg_count, AngaraObject* args) {
    if (arg_count < 3) {
        ang_api->throw_error("calltest.invoke(fn, a, b) expects 3 arguments.");
        return ang_nil();
    }

    AngaraObject fn = args[0];
    AngaraObject argv[2] = { args[1], args[2] };

    AngaraObject result = ang_api->call(fn, 2, argv);
    return result;
}

/* invoke0(fn) — calls fn() with no arguments via ang_api->call(). */
AngaraObject Angara_calltest_invoke0(int arg_count, AngaraObject* args) {
    if (arg_count < 1) {
        ang_api->throw_error("calltest.invoke0(fn) expects 1 argument.");
        return ang_nil();
    }
    return ang_api->call(args[0], 0, NULL);
}

/* invoke1(fn, x) — calls fn(x) with one argument via ang_api->call(). */
AngaraObject Angara_calltest_invoke1(int arg_count, AngaraObject* args) {
    if (arg_count < 2) {
        ang_api->throw_error("calltest.invoke1(fn, x) expects 2 arguments.");
        return ang_nil();
    }
    AngaraObject argv[1] = { args[1] };
    return ang_api->call(args[0], 1, argv);
}

static const AngaraFuncDef CALLTEST_EXPORTS[] = {
    {"invoke",  Angara_calltest_invoke,  "aai->a", NULL},
    {"invoke0", Angara_calltest_invoke0, "a->a",   NULL},
    {"invoke1", Angara_calltest_invoke1, "aa->a",  NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(calltest) {
    ang_api = api;
    *def_count = (sizeof(CALLTEST_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return CALLTEST_EXPORTS;
}

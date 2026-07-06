#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Angara.h"

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


AngaraObject Angara_assert_that(int arg_count, AngaraObject* args) {

    if (!ang_api->truthy(args[0])) {
        const char* msg = ang_api->as_cstr(args[1]);
        throw_assert_error(NULL, ang_nil(), ang_nil(), msg);
    }

    return ang_nil();
}

AngaraObject Angara_assert_eq(int arg_count, AngaraObject* args) {

    if (!ang_api->equals(args[0], args[1])) {
        throw_assert_error("EQUAL", args[0], args[1], ang_api->as_cstr(args[2]));
    }

    return ang_nil();
}

AngaraObject Angara_assert_ne(int arg_count, AngaraObject* args) {

    if (ang_api->equals(args[0], args[1])) {
        throw_assert_error("NOT EQUAL", args[0], args[1], ang_api->as_cstr(args[2]));
    }

    return ang_nil();
}

AngaraObject Angara_assert_fail(int arg_count, AngaraObject* args) {

    char buffer[1024];
    snprintf(buffer, 1024, "Explicit Failure: %s", ang_api->as_cstr(args[0]));
    ang_api->throw_error(buffer);

    return ang_nil();
}


/* =========================================================================
   TestRunner — collects results and produces a summary
   ========================================================================= */

typedef struct {
    AngaraObject results;   /* list<{name:string, passed:bool, error:string?}> */
} TestRunnerData;

static void finalize_runner(void* data) {
    TestRunnerData* tr = (TestRunnerData*)data;
    ang_api->decref(tr->results);
    free(tr);
}

AngaraObject Angara_assert_runner_new(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    TestRunnerData* tr = (TestRunnerData*)calloc(1, sizeof(TestRunnerData));
    tr->results = ang_api->list_new();
    return ang_api->native_instance_new(tr, finalize_runner, "TestRunner");
}

AngaraObject Angara_TestRunner_record(int arg_count, AngaraObject* args) {
    /* args: self, name:string, passed:bool, error:string? */
    if (arg_count < 3) return ang_nil();
    TestRunnerData* tr = (TestRunnerData*)ang_api->native_instance_data(args[0]);
    if (!tr) return ang_nil();

    AngaraObject entry = ang_api->record_new();
    ang_api->record_set(entry, "name", args[1]);
    ang_api->record_set(entry, "passed", args[2]);
    if (arg_count >= 4 && ang_is_obj(args[3])) {
        ang_api->record_set(entry, "error", args[3]);
    }
    ang_api->list_push(tr->results, entry);
    ang_api->decref(entry);
    return ang_nil();
}

AngaraObject Angara_TestRunner_summary(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TestRunnerData* tr = (TestRunnerData*)ang_api->native_instance_data(args[0]);
    if (!tr) return ang_nil();

    size_t len = ang_api->list_len(tr->results);
    int64_t passed = 0, failed = 0;
    for (size_t i = 0; i < len; i++) {
        AngaraObject entry = ang_api->list_get(tr->results, (int64_t)i);
        AngaraObject p = ang_api->record_get(entry, "passed");
        if (ang_api->truthy(p)) passed++; else failed++;
        ang_api->decref(p);
        ang_api->decref(entry);
    }

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "passed", ang_i64(passed));
    ang_api->record_set(rec, "failed", ang_i64(failed));
    ang_api->record_set(rec, "total",  ang_i64((int64_t)len));
    return rec;
}

AngaraObject Angara_TestRunner_results(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TestRunnerData* tr = (TestRunnerData*)ang_api->native_instance_data(args[0]);
    if (!tr) return ang_nil();

    /* return a shallow copy of the results list */
    size_t len = ang_api->list_len(tr->results);
    AngaraObject copy = ang_api->list_new();
    for (size_t i = 0; i < len; i++) {
        AngaraObject entry = ang_api->list_get(tr->results, (int64_t)i);
        ang_api->list_push(copy, entry);
        ang_api->decref(entry);
    }
    return copy;
}

AngaraObject Angara_TestRunner_report(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TestRunnerData* tr = (TestRunnerData*)ang_api->native_instance_data(args[0]);
    if (!tr) return ang_nil();

    size_t len = ang_api->list_len(tr->results);
    int64_t passed = 0, failed = 0;

    /* build a formatted report string */
    size_t cap = 4096, out_len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return ang_api->string("");

    for (size_t i = 0; i < len; i++) {
        AngaraObject entry = ang_api->list_get(tr->results, (int64_t)i);
        AngaraObject p = ang_api->record_get(entry, "passed");
        AngaraObject n = ang_api->record_get(entry, "name");

        int is_pass = ang_api->truthy(p);
        if (is_pass) passed++; else failed++;

        const char* name_str = ang_api->as_cstr(n);
        const char* status = is_pass ? "PASS" : "FAIL";
        char line[1024];
        int line_len = snprintf(line, sizeof(line), "  [%s] %s\n", status, name_str);

        if (!is_pass) {
            AngaraObject err = ang_api->record_get(entry, "error");
            if (ang_is_obj(err)) {
                const char* err_str = ang_api->as_cstr(err);
                line_len += snprintf(line + line_len, sizeof(line) - line_len,
                                     "         %s\n", err_str);
            }
            ang_api->decref(err);
        }

        if (out_len + (size_t)line_len + 1 >= cap) {
            cap = (out_len + (size_t)line_len) * 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return ang_api->string(""); }
            buf = nb;
        }
        memcpy(buf + out_len, line, (size_t)line_len);
        out_len += (size_t)line_len;

        ang_api->decref(p);
        ang_api->decref(n);
        ang_api->decref(entry);
    }

    /* summary footer */
    char footer[256];
    int footer_len = snprintf(footer, sizeof(footer),
                              "\n%d passed, %d failed, %zu total\n",
                              (int)passed, (int)failed, len);
    if (out_len + (size_t)footer_len + 1 >= cap) {
        cap = out_len + (size_t)footer_len + 1;
        char* nb = (char*)realloc(buf, cap);
        if (!nb) { free(buf); return ang_api->string(""); }
        buf = nb;
    }
    memcpy(buf + out_len, footer, (size_t)footer_len);
    out_len += (size_t)footer_len;

    return ang_api->string_no_copy(buf, out_len);
}


static const AngaraMethodDef RUNNER_METHODS[] = {
    {"record",  (AngaraMethodFn)Angara_TestRunner_record,  "sb{}?->n"},
    {"summary", (AngaraMethodFn)Angara_TestRunner_summary, "->{}"},
    {"results", (AngaraMethodFn)Angara_TestRunner_results, "->l<{}>"},
    {"report",  (AngaraMethodFn)Angara_TestRunner_report,  "->s"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef RUNNER_CLASS = { "TestRunner", NULL, RUNNER_METHODS };

static const AngaraFuncDef ASSERT_EXPORTS[] = {
    {"that",   Angara_assert_that,   "as->n",  NULL},
    {"eq",     Angara_assert_eq,     "aas->n", NULL},
    {"ne",     Angara_assert_ne,     "aas->n", NULL},
    {"fail",   Angara_assert_fail,   "s->n",   NULL},
    {"runner", Angara_assert_runner_new, "->TestRunner", &RUNNER_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(assert) {
    ang_api = api;
    *def_count = (sizeof(ASSERT_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ASSERT_EXPORTS;
}
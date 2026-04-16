//
// os.c — Angara OS module (rewritten for 16-byte ABI + vtable)
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

// os.getenv(name) -> string?
AngaraObject Angara_os_getenv(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("os.getenv() requires one string argument.");
        return ang_nil();
    }
    char* value = getenv(ang_api->as_cstr(args[0]));
    if (!value) return ang_nil();
    return ang_api->string(value);
}

// os.run(command) -> record{stdout: string, exit_code: i64}
AngaraObject Angara_os_run(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("os.run() requires one string argument.");
        return ang_nil();
    }

    const char* command = ang_api->as_cstr(args[0]);
    char full_command[4096];
    snprintf(full_command, sizeof(full_command), "%s 2>&1", command);

    FILE* pipe = popen(full_command, "r");
    if (!pipe) {
        char buf[256];
        snprintf(buf, 256, "os.run() failed: %s", strerror(errno));
        ang_api->throw_error(buf);
        return ang_nil();
    }

    char buffer[128];
    AngaraObject stdout_obj = ang_api->string("");

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        AngaraObject chunk = ang_api->string(buffer);
        AngaraObject new_stdout = ang_api->string_concat(stdout_obj, chunk);
        ang_api->decref(stdout_obj);
        ang_api->decref(chunk);
        stdout_obj = new_stdout;
    }

    int status = pclose(pipe);
    int exit_code = WEXITSTATUS(status);

    AngaraObject result = ang_api->record_new();
    ang_api->record_set(result, "stdout", stdout_obj);
    ang_api->record_set(result, "exit_code", ang_i64(exit_code));
    ang_api->decref(stdout_obj);

    return result;
}

static const AngaraFuncDef OS_EXPORTS[] = {
    {"getenv", Angara_os_getenv, "s->s", NULL},
    {"run",    Angara_os_run,    "s->{}", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(os) {
    ang_api = api;
    *def_count = (sizeof(OS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return OS_EXPORTS;
}
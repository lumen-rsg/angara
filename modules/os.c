#include "../runtime/angara_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// --- Angara-Exported Function: os.getenv ---
// Angara signature: func getenv(name as string) -> string?
AngaraObject Angara_os_getenv(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("os.getenv() requires one string argument for the variable name.");
        return angara_create_nil();
    }

    const char* var_name = AS_CSTRING(args[0]);
    char* value = getenv(var_name);

    if (value == NULL) {
        // If the environment variable is not set, getenv returns NULL.
        // We correctly return an Angara `nil`.
        return angara_create_nil();
    } else {
        // If it is set, we return an Angara string.
        return angara_create_string(value);
    }
}


// --- Angara-Exported Function: os.run ---
// Angara signature: func run(command as string) -> record
// Returns a record: { stdout: string, stderr: string, exit_code: i64 }
AngaraObject Angara_os_run(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("os.run() requires one string argument for the command to execute.");
        return angara_create_nil();
    }

    const char* command = AS_CSTRING(args[0]);
    char full_command[4096];

    // We redirect stderr to stdout to capture everything in one stream.
    snprintf(full_command, sizeof(full_command), "%s 2>&1", command);

    FILE* pipe = popen(full_command, "r");
    if (!pipe) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "os.run() failed to execute command '%s': %s", command, strerror(errno));
        angara_throw_error(err_buf);
        return angara_create_nil();
    }

    // Read the entire output of the command into a buffer.
    char buffer[128];
    AngaraObject stdout_obj = angara_create_string(""); // Start with an empty string

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        AngaraObject chunk = angara_create_string(buffer);
        AngaraObject new_stdout = angara_string_concat(stdout_obj, chunk);
        angara_decref(stdout_obj);
        angara_decref(chunk);
        stdout_obj = new_stdout;
    }

    int status = pclose(pipe);
    int exit_code = WEXITSTATUS(status);

    // Create the result record.
    AngaraObject result_record = angara_record_new();
    angara_record_set(result_record, "stdout", stdout_obj);
    angara_record_set(result_record, "exit_code", angara_create_i64(exit_code));

    // The record now owns the stdout_obj, so we can decref our local reference.
    angara_decref(stdout_obj);

    return result_record;
}


// --- ABI Definition Table ---
const AngaraFuncDef OS_EXPORTS[] = {
    {
        "getenv",
        Angara_os_getenv,
        "s->s?", // Takes string, returns optional string
        NULL
    },
    {
        "run",
        Angara_os_run,
        "s->{}", // Takes string, returns a generic record
        NULL
    },
    {NULL, NULL, NULL, NULL}
};

// --- Module Entry Point ---
const AngaraFuncDef* Angara_os_Init(int* def_count) {
    *def_count = (sizeof(OS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return OS_EXPORTS;
}
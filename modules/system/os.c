/// Angara OS module — getenv, setenv, exit, system(), command execution.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include "../_wincompat.h"
#include <process.h>  // _spawnvp, _P_WAIT
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

AngaraObject Angara_os_getenv(int arg_count, AngaraObject args[]) {
    if (arg_count < 1) { ang_api->throw_error("os.getenv: expected 1 argument"); return ang_nil(); }
    char* value = getenv(ang_api->as_cstr(args[0]));
    if (!value) return ang_nil();
    return ang_api->string(value);
}

/* split a command string into an argv array (simple whitespace tokeniser).
   caller must free each element and the outer array. */
static char** split_cmd(const char* cmd, int* out_argc) {
    /* make a mutable copy */
    char* buf = strdup(cmd);
    if (!buf) { *out_argc = 0; return NULL; }

    int cap = 8, argc = 0;
    char** argv = (char**)malloc(cap * sizeof(char*));
    if (!argv) { free(buf); *out_argc = 0; return NULL; }

    char* saveptr = NULL;
    char* token = strtok_r(buf, " \t\r\n", &saveptr);
    while (token) {
        if (argc + 1 >= cap) {
            cap *= 2;
            char** na = (char**)realloc(argv, cap * sizeof(char*));
            if (!na) { free(buf); free(argv); *out_argc = 0; return NULL; }
            argv = na;
        }
        argv[argc++] = strdup(token);
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    argv[argc] = NULL;
    free(buf);
    *out_argc = argc;
    return argv;
}

static void free_argv(char** argv) {
    if (!argv) return;
    for (int i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

AngaraObject Angara_os_run(int arg_count, AngaraObject args[]) {

    if (arg_count < 1) { ang_api->throw_error("os.run: expected 1 argument"); return ang_nil(); }
    const char* command = ang_api->as_cstr(args[0]);

    int argc = 0;
    char** argv = split_cmd(command, &argc);
    if (!argv || argc == 0) {
        free_argv(argv);
        ang_api->throw_error("os.run(): empty command.");
        return ang_nil();
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        free_argv(argv);
        ang_api->throw_error("os.run(): failed to create pipe.");
        return ang_nil();
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        free_argv(argv);
        ang_api->throw_error("os.run(): fork failed.");
        return ang_nil();
    }

    if (pid == 0) {
        /* child: redirect stderr→stdout, exec */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    /* parent: read output */
    close(pipefd[1]);
    free_argv(argv);

    char* out_buf = NULL;
    size_t out_len = 0, out_cap = 4096;
    out_buf = (char*)malloc(out_cap);

    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        if (out_len + (size_t)n >= out_cap) {
            out_cap = (out_len + (size_t)n) * 2;
            char* nb = (char*)realloc(out_buf, out_cap);
            if (!nb) break;
            out_buf = nb;
        }
        memcpy(out_buf + out_len, tmp, (size_t)n);
        out_len += (size_t)n;
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    AngaraObject result = ang_api->record_new();
    ang_api->record_set(result, "exit_code", ang_i64(exit_code));

    if (out_buf && out_len > 0) {
        ang_api->record_set(result, "stdout", ang_api->string_no_copy(out_buf, out_len));
    } else {
        free(out_buf);
        ang_api->record_set(result, "stdout", ang_api->string(""));
    }

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
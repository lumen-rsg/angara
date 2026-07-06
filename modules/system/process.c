/// Angara process module — child process spawning with pipe I/O.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

/* Simple whitespace tokenizer for command strings.
   Splits on whitespace, respecting single and double quotes.
   Returns a NULL-terminated argv array; caller must free each element and the array.
   Sets *argc_out to the number of arguments. */
static char** tokenize_command(const char* cmd, int* argc_out) {
    int cap = 8;
    int count = 0;
    char** argv = (char**)malloc((cap + 1) * sizeof(char*));
    if (!argv) { *argc_out = 0; return NULL; }

    const char* p = cmd;
    while (*p) {
        /* skip leading whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;

        const char* start;
        const char* end;
        char quote = 0;

        if (*p == '\'' || *p == '"') {
            quote = *p;
            p++;
            start = p;
            while (*p && *p != quote) p++;
            end = p;
            if (*p == quote) p++;
        } else {
            start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            end = p;
        }

        size_t len = (size_t)(end - start);
        if (count >= cap) {
            cap *= 2;
            char** tmp = (char**)realloc(argv, (cap + 1) * sizeof(char*));
            if (!tmp) {
                for (int i = 0; i < count; i++) free(argv[i]);
                free(argv);
                *argc_out = 0;
                return NULL;
            }
            argv = tmp;
        }
        argv[count] = (char*)malloc(len + 1);
        if (!argv[count]) {
            for (int i = 0; i < count; i++) free(argv[i]);
            free(argv);
            *argc_out = 0;
            return NULL;
        }
        memcpy(argv[count], start, len);
        argv[count][len] = '\0';
        count++;
    }
    argv[count] = NULL;
    *argc_out = count;
    return argv;
}

AngaraObject Angara_process_exec(int arg_count, AngaraObject* args) {

    if (arg_count < 1) { ang_api->throw_error("process.exec: expected 1 argument"); return ang_nil(); }
    const char* cmd = ang_api->as_cstr(args[0]);

    int argc = 0;
    char** argv = tokenize_command(cmd, &argc);
    if (!argv || argc == 0) {
        ang_api->throw_error("exec: empty or invalid command.");
        return ang_nil();
    }

    int stdout_pipe[2];
    if (pipe(stdout_pipe) < 0) {
        for (int i = 0; i < argc; i++) free(argv[i]);
        free(argv);
        ang_api->throw_error("exec: pipe failed.");
        return ang_nil();
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        for (int i = 0; i < argc; i++) free(argv[i]);
        free(argv);
        ang_api->throw_error("exec: fork failed.");
        return ang_nil();
    }

    if (pid == 0) {
        /* child */
        close(stdout_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    /* parent */
    close(stdout_pipe[1]);
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);

    char* output = NULL;
    size_t output_len = 0;
    size_t output_cap = 4096;
    output = (char*)malloc(output_cap);
    if (!output) {
        close(stdout_pipe[0]);
        waitpid(pid, NULL, 0);
        ang_api->throw_error("exec: out of memory.");
        return ang_nil();
    }

    char chunk[4096];
    ssize_t n;
    while ((n = read(stdout_pipe[0], chunk, sizeof(chunk))) > 0 ||
           (n == -1 && errno == EINTR)) {
        if (n <= 0) continue;
        if (output_len + (size_t)n >= output_cap) {
            output_cap = (output_len + (size_t)n) * 2;
            char* new_output = (char*)realloc(output, output_cap);
            if (!new_output) {
                free(output);
                close(stdout_pipe[0]);
                waitpid(pid, NULL, 0);
                ang_api->throw_error("exec: out of memory.");
                return ang_nil();
            }
            output = new_output;
        }
        memcpy(output + output_len, chunk, (size_t)n);
        output_len += (size_t)n;
    }
    close(stdout_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    (void)status;

    if (output_len == 0) {
        free(output);
        return ang_api->string("");
    }

    return ang_api->string_no_copy(output, output_len);
}

AngaraObject Angara_process_run(int arg_count, AngaraObject* args) {

    const char* cmd = ang_api->as_cstr(args[0]);

    int argc_build = 1;
    if (arg_count >= 2 && IS_LIST(args[1])) {
        argc_build += (int)ang_api->list_len(args[1]);
    }

    char** argv = (char**)malloc((argc_build + 1) * sizeof(char*));
    argv[0] = strdup(cmd);
    int ai = 1;
    if (arg_count >= 2 && IS_LIST(args[1])) {
        size_t alen = ang_api->list_len(args[1]);
        for (size_t i = 0; i < alen; i++) {
            AngaraObject elem = ang_api->list_get(args[1], (int64_t)i);
            argv[ai++] = IS_STR(elem) ? strdup(ang_api->as_cstr(elem)) : strdup("");
            ang_api->decref(elem);
        }
    }
    argv[ai] = NULL;

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    pid_t pid = fork();
    if (pid < 0) {
        /* M19: free each strdup'd argv element before the array */
        for (int i = 0; i < argc_build; i++) free(argv[i]);
        free(argv);
        if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
        if (stderr_pipe[0] >= 0) { close(stderr_pipe[0]); close(stderr_pipe[1]); }
        ang_api->throw_error("run: fork failed.");
        return ang_nil();
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (arg_count >= 3 && ang_is_obj(args[2]) && ang_api->obj_type(args[2]) == ANG_OBJ_RECORD) {
            AngaraObject cwd_val = ang_api->record_get(args[2], "cwd");
            if (IS_STR(cwd_val)) {
                chdir(ang_api->as_cstr(cwd_val));
            }
            ang_api->decref(cwd_val);
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    char* out_buf = NULL;
    size_t out_len = 0, out_cap = 4096;
    out_buf = (char*)malloc(out_cap);

    char tmp[4096];
    ssize_t n;
    while ((n = read(stdout_pipe[0], tmp, sizeof(tmp))) > 0 ||
           (n == -1 && errno == EINTR)) {
        if (n <= 0) continue;
        if (out_len + (size_t)n >= out_cap) {
            out_cap = (out_len + (size_t)n) * 2;
            char* nb = (char*)realloc(out_buf, out_cap);
            if (!nb) break;
            out_buf = nb;
        }
        memcpy(out_buf + out_len, tmp, (size_t)n);
        out_len += (size_t)n;
    }
    close(stdout_pipe[0]);

    char* err_buf = NULL;
    size_t err_len = 0, err_cap = 4096;
    err_buf = (char*)malloc(err_cap);

    while ((n = read(stderr_pipe[0], tmp, sizeof(tmp))) > 0 ||
           (n == -1 && errno == EINTR)) {
        if (n <= 0) continue;
        if (err_len + (size_t)n >= err_cap) {
            err_cap = (err_len + (size_t)n) * 2;
            char* nb = (char*)realloc(err_buf, err_cap);
            if (!nb) break;
            err_buf = nb;
        }
        memcpy(err_buf + err_len, tmp, (size_t)n);
        err_len += (size_t)n;
    }
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    for (int i = 0; i < argc_build; i++) free(argv[i]);
    free(argv);

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "exit_code", ang_i64(exit_code));

    if (out_buf && out_len > 0) {
        ang_api->record_set(rec, "stdout", ang_api->string_no_copy(out_buf, out_len));
    } else {
        free(out_buf);
        ang_api->record_set(rec, "stdout", ang_api->string(""));
    }

    if (err_buf && err_len > 0) {
        ang_api->record_set(rec, "stderr", ang_api->string_no_copy(err_buf, err_len));
    } else {
        free(err_buf);
        ang_api->record_set(rec, "stderr", ang_api->string(""));
    }

    return rec;
}

typedef struct {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int exited;
    int exit_code;
} ProcessData;

static void finalize_process(void* data) {
    ProcessData* pd = (ProcessData*)data;
    if (pd->stdin_fd >= 0) close(pd->stdin_fd);
    if (pd->stdout_fd >= 0) close(pd->stdout_fd);
    if (pd->stderr_fd >= 0) close(pd->stderr_fd);
    if (pd->pid > 0 && !pd->exited) {
        kill(pd->pid, SIGTERM);
        waitpid(pd->pid, NULL, 0);
    }
    free(pd);
}

AngaraObject Angara_process_spawn(int arg_count, AngaraObject* args) {

    const char* cmd = ang_api->as_cstr(args[0]);

    int argc_build = 1;
    if (arg_count >= 2 && IS_LIST(args[1])) {
        argc_build += (int)ang_api->list_len(args[1]);
    }

    char** argv = (char**)malloc((argc_build + 1) * sizeof(char*));
    argv[0] = strdup(cmd);
    int ai = 1;
    if (arg_count >= 2 && IS_LIST(args[1])) {
        size_t alen = ang_api->list_len(args[1]);
        for (size_t i = 0; i < alen; i++) {
            AngaraObject elem = ang_api->list_get(args[1], (int64_t)i);
            argv[ai++] = IS_STR(elem) ? strdup(ang_api->as_cstr(elem)) : strdup("");
            ang_api->decref(elem);
        }
    }
    argv[ai] = NULL;

    int in_pipe[2], out_pipe[2], err_pipe[2];
    pipe(in_pipe);
    pipe(out_pipe);
    pipe(err_pipe);

    pid_t pid = fork();
    if (pid < 0) {
        /* M19: free each strdup'd argv element before the array */
        for (int i = 0; i < argc_build; i++) free(argv[i]);
        free(argv);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        ang_api->throw_error("spawn: fork failed.");
        return ang_nil();
    }

    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    for (int i = 0; i < argc_build; i++) free(argv[i]);
    free(argv);

    ProcessData* pd = (ProcessData*)calloc(1, sizeof(ProcessData));
    pd->pid = pid;
    pd->stdin_fd = in_pipe[1];
    pd->stdout_fd = out_pipe[0];
    pd->stderr_fd = err_pipe[0];
    pd->exited = 0;
    pd->exit_code = -1;

    return ang_api->native_instance_new(pd, finalize_process, "Process");
}

AngaraObject Angara_Process_write(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("Process.write: expected 2 arguments"); return ang_nil(); }
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    if (!pd || pd->stdin_fd < 0) return ang_nil();

    const char* data = ang_api->as_cstr(args[1]);
    size_t len = ang_api->str_len(args[1]);
    ssize_t written = write(pd->stdin_fd, data, len);
    (void)written;  /* best-effort write; caller can check with wait/exit status */
    return ang_nil();
}

AngaraObject Angara_Process_close_stdin(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Process.close_stdin: expected 1 argument"); return ang_nil(); }
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    if (pd && pd->stdin_fd >= 0) {
        close(pd->stdin_fd);
        pd->stdin_fd = -1;
    }
    return ang_nil();
}

AngaraObject Angara_Process_read_stdout(int arg_count, AngaraObject* args) {
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    if (!pd || pd->stdout_fd < 0) return ang_nil();

    size_t buf_size = 4096;
    if (arg_count >= 2 && ang_is_i64(args[1])) buf_size = (size_t)ang_as_i64(args[1]);
    if (buf_size == 0) buf_size = 4096;

    char* buf = (char*)malloc(buf_size);
    ssize_t n;
    do {
        n = read(pd->stdout_fd, buf, buf_size);
    } while (n == -1 && errno == EINTR);
    if (n <= 0) { free(buf); return ang_nil(); }
    return ang_api->string_no_copy(buf, (size_t)n);
}

AngaraObject Angara_Process_read_stderr(int arg_count, AngaraObject* args) {
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    if (!pd || pd->stderr_fd < 0) return ang_nil();

    size_t buf_size = 4096;
    if (arg_count >= 2 && ang_is_i64(args[1])) buf_size = (size_t)ang_as_i64(args[1]);
    if (buf_size == 0) buf_size = 4096;

    char* buf = (char*)malloc(buf_size);
    ssize_t n;
    do {
        n = read(pd->stderr_fd, buf, buf_size);
    } while (n == -1 && errno == EINTR);
    if (n <= 0) { free(buf); return ang_nil(); }
    return ang_api->string_no_copy(buf, (size_t)n);
}

AngaraObject Angara_Process_wait(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Process.wait: expected 1 argument"); return ang_nil(); }
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    if (!pd || pd->exited) return ang_i64(pd->exit_code);

    int status;
    waitpid(pd->pid, &status, 0);
    pd->exited = 1;
    pd->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return ang_i64(pd->exit_code);
}

AngaraObject Angara_Process_pid(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Process.pid: expected 1 argument"); return ang_nil(); }
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    return ang_i64(pd ? (int64_t)pd->pid : -1);
}

AngaraObject Angara_Process_kill(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Process.kill: expected 1 argument"); return ang_nil(); }
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    if (pd && pd->pid > 0 && !pd->exited) {
        kill(pd->pid, SIGTERM);
    }
    return ang_nil();
}

AngaraObject Angara_Process_is_alive(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Process.is_alive: expected 1 argument"); return ang_nil(); }
    ProcessData* pd = (ProcessData*)ang_api->native_instance_data(args[0]);
    if (!pd || pd->exited) return ang_bool(false);
    int status;
    pid_t result = waitpid(pd->pid, &status, WNOHANG);
    if (result == 0) return ang_bool(true);
    pd->exited = 1;
    pd->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return ang_bool(false);
}

AngaraObject Angara_process_getpid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getpid());
}

AngaraObject Angara_process_getppid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getppid());
}

AngaraObject Angara_process_kill(int arg_count, AngaraObject* args) {
    int sig = SIGTERM;
    if (arg_count >= 2 && ang_is_i64(args[1])) sig = (int)ang_as_i64(args[1]);
    kill((pid_t)ang_as_i64(args[0]), sig);
    return ang_nil();
}

static const AngaraMethodDef PROCESS_METHODS[] = {
    {"write",         (AngaraMethodFn)Angara_Process_write,         "s->n"},
    {"close_stdin",   (AngaraMethodFn)Angara_Process_close_stdin,   "->n"},
    {"read_stdout",   (AngaraMethodFn)Angara_Process_read_stdout,   "i?->s?"},
    {"read_stderr",   (AngaraMethodFn)Angara_Process_read_stderr,   "i?->s?"},
    {"wait",          (AngaraMethodFn)Angara_Process_wait,          "->i"},
    {"pid",           (AngaraMethodFn)Angara_Process_pid,           "->i"},
    {"kill",          (AngaraMethodFn)Angara_Process_kill,          "->n"},
    {"is_alive",      (AngaraMethodFn)Angara_Process_is_alive,      "->b"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef PROCESS_CLASS_DEF = { "Process", NULL, PROCESS_METHODS };

static const AngaraFuncDef PROCESS_EXPORTS[] = {
    {"exec",    Angara_process_exec,    "s->s",   NULL},
    {"run",     Angara_process_run,     "sl<s>?{}?->{}", NULL},
    {"spawn",   Angara_process_spawn,   "sl<s>?->Process", &PROCESS_CLASS_DEF},
    {"getpid",  Angara_process_getpid,  "->i",    NULL},
    {"getppid", Angara_process_getppid, "->i",    NULL},
    {"kill",    Angara_process_kill,    "ii?->n", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(process) {
    ang_api = api;
    *def_count = (sizeof(PROCESS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return PROCESS_EXPORTS;
}
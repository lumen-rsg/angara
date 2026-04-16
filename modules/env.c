//
// env.c — Angara environment module
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

// env.get(name) -> string?
AngaraObject Angara_env_get(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("env.get(name) expects one string argument.");
        return ang_nil();
    }
    const char* name = ang_api->as_cstr(args[0]);
    char* value = getenv(name);
    if (!value) return ang_nil();
    return ang_api->string(value);
}

// env.set(name, value) -> nil
AngaraObject Angara_env_set(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("env.set(name, value) expects two string arguments.");
        return ang_nil();
    }
    const char* name = ang_api->as_cstr(args[0]);
    const char* value = ang_api->as_cstr(args[1]);
    if (setenv(name, value, 1) != 0) {
        ang_api->throw_error("env.set: failed to set environment variable.");
    }
    return ang_nil();
}

// env.unset(name) -> nil
AngaraObject Angara_env_unset(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("env.unset(name) expects one string argument.");
        return ang_nil();
    }
    unsetenv(ang_api->as_cstr(args[0]));
    return ang_nil();
}

// env.all() -> record
AngaraObject Angara_env_all(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        ang_api->throw_error("env.all() expects no arguments.");
        return ang_nil();
    }
    AngaraObject rec = ang_api->record_new();
    extern char** environ;
    if (environ) {
        for (char** env = environ; *env != NULL; env++) {
            char* entry = *env;
            char* eq = strchr(entry, '=');
            if (eq && eq != entry) {
                size_t name_len = eq - entry;
                char* name_buf = (char*)malloc(name_len + 1);
                memcpy(name_buf, entry, name_len);
                name_buf[name_len] = '\0';
                const char* value = eq + 1;
                ang_api->record_set(rec, name_buf, ang_api->string(value));
                free(name_buf);
            }
        }
    }
    return rec;
}

// env.args() -> list<string>
// Returns command-line arguments. Must be set externally or returns empty list.
// For now, reads /proc/self/cmdline on Linux or uses _NSGetArgv on macOS.
AngaraObject Angara_env_args(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        ang_api->throw_error("env.args() expects no arguments.");
        return ang_nil();
    }
    AngaraObject list = ang_api->list_new();

#ifdef __APPLE__
    // macOS: use _NSGetArgv and _NSGetArgc
    #include <crt_externs.h>
    int argc = *_NSGetArgc();
    char** argv = *_NSGetArgv();
    if (argv) {
        for (int i = 0; i < argc; i++) {
            if (argv[i]) {
                ang_api->list_push(list, ang_api->string(argv[i]));
            }
        }
    }
#elif defined(__linux__)
    // Linux: read /proc/self/cmdline
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (f) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), f)) {
            // cmdline uses null bytes as separators, but fgets stops at \n
            // Better to read byte by byte
        }
        fclose(f);
    }
    // Fallback: read /proc/self/cmdline properly
    FILE* pf = fopen("/proc/self/cmdline", "rb");
    if (pf) {
        char arg_buf[4096];
        while (1) {
            size_t i = 0;
            int c;
            while ((c = fgetc(pf)) != EOF && c != '\0' && i < sizeof(arg_buf) - 1) {
                arg_buf[i++] = (char)c;
            }
            if (i == 0 && c == EOF) break;
            arg_buf[i] = '\0';
            ang_api->list_push(list, ang_api->string(arg_buf));
            if (c == EOF) break;
        }
        fclose(pf);
    }
#endif

    return list;
}

// env.pid() -> i64
AngaraObject Angara_env_pid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getpid());
}

// env.ppid() -> i64
AngaraObject Angara_env_ppid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getppid());
}

// env.cwd() -> string
AngaraObject Angara_env_cwd(int arg_count, AngaraObject* args) {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        return ang_api->string(buf);
    }
    ang_api->throw_error("env.cwd: failed to get current directory.");
    return ang_nil();
}

// env.chdir(path) -> nil
AngaraObject Angara_env_chdir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("env.chdir(path) expects one string argument.");
        return ang_nil();
    }
    if (chdir(ang_api->as_cstr(args[0])) != 0) {
        ang_api->throw_error("env.chdir: failed to change directory.");
    }
    return ang_nil();
}

// env.hostname() -> string
AngaraObject Angara_env_hostname(int arg_count, AngaraObject* args) {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return ang_api->string(buf);
    }
    ang_api->throw_error("env.hostname: failed to get hostname.");
    return ang_nil();
}

// env.user() -> string
AngaraObject Angara_env_user(int arg_count, AngaraObject* args) {
    const char* user = getenv("USER");
    if (!user) user = getenv("LOGNAME");
    if (!user) user = getenv("USERNAME");
    if (user) return ang_api->string(user);
    return ang_api->string("unknown");
}

// env.home() -> string
AngaraObject Angara_env_home(int arg_count, AngaraObject* args) {
    const char* home = getenv("HOME");
    if (home) return ang_api->string(home);
    return ang_api->string("/tmp");
}

// env.shell() -> string
AngaraObject Angara_env_shell(int arg_count, AngaraObject* args) {
    const char* shell = getenv("SHELL");
    if (shell) return ang_api->string(shell);
    return ang_api->string("/bin/sh");
}

// --- Export Table ---

static const AngaraFuncDef ENV_EXPORTS[] = {
    {"get",      Angara_env_get,      "s->s?",  NULL},
    {"set",      Angara_env_set,      "ss->n",  NULL},
    {"unset",    Angara_env_unset,    "s->n",   NULL},
    {"all",      Angara_env_all,      "->{}",   NULL},
    {"args",     Angara_env_args,     "->l<s>", NULL},
    {"pid",      Angara_env_pid,      "->i",    NULL},
    {"ppid",     Angara_env_ppid,     "->i",    NULL},
    {"cwd",      Angara_env_cwd,      "->s",    NULL},
    {"chdir",    Angara_env_chdir,    "s->n",   NULL},
    {"hostname", Angara_env_hostname, "->s",    NULL},
    {"user",     Angara_env_user,     "->s",    NULL},
    {"home",     Angara_env_home,     "->s",    NULL},
    {"shell",    Angara_env_shell,    "->s",    NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(env) {
    ang_api = api;
    *def_count = (sizeof(ENV_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ENV_EXPORTS;
}
/// Angara env module — environment variable get/set/list operations.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

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

AngaraObject Angara_env_set(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("env.set(name, value) expects two string arguments.");
        return ang_nil();
    }
    const char* name = ang_api->as_cstr(args[0]);
    const char* value = ang_api->as_cstr(args[1]);
#ifdef _WIN32
    if (_putenv_s(name, value) != 0) {
        ang_api->throw_error("env.set: failed to set environment variable.");
    }
#else
    if (setenv(name, value, 1) != 0) {
        ang_api->throw_error("env.set: failed to set environment variable.");
    }
#endif
    return ang_nil();
}

AngaraObject Angara_env_unset(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("env.unset(name) expects one string argument.");
        return ang_nil();
    }
#ifdef _WIN32
    // _putenv_s with "NAME=" removes the variable
    char buf[512];
    snprintf(buf, sizeof(buf), "%s=", ang_api->as_cstr(args[0]));
    _putenv(buf);
#else
    unsetenv(ang_api->as_cstr(args[0]));
#endif
    return ang_nil();
}

AngaraObject Angara_env_all(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        ang_api->throw_error("env.all() expects no arguments.");
        return ang_nil();
    }
    AngaraObject rec = ang_api->record_new();
#ifdef _WIN32
    // Windows: use GetEnvironmentStringsW, convert to UTF-8
    LPWCH env_block = GetEnvironmentStringsW();
    if (env_block) {
        LPWCH p = env_block;
        while (*p) {
            // Convert wide string to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, p, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                char* entry = (char*)malloc(len);
                WideCharToMultiByte(CP_UTF8, 0, p, -1, entry, len, NULL, NULL);
                char* eq = strchr(entry, '=');
                if (eq && eq != entry) {
                    size_t name_len = eq - entry;
                    char* name_buf = (char*)malloc(name_len + 1);
                    memcpy(name_buf, entry, name_len);
                    name_buf[name_len] = '\0';
                    ang_api->record_set(rec, name_buf, ang_api->string(eq + 1));
                    free(name_buf);
                }
                free(entry);
            }
            p += wcslen(p) + 1;
        }
        FreeEnvironmentStringsW(env_block);
    }
#else
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
#endif
    return rec;
}

AngaraObject Angara_env_args(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        ang_api->throw_error("env.args() expects no arguments.");
        return ang_nil();
    }
    AngaraObject list = ang_api->list_new();

#ifdef _WIN32
    // Windows: use GetCommandLineW + CommandLineToArgvW
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wargv) {
        for (int i = 0; i < argc; i++) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                char* arg = (char*)malloc(len);
                WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg, len, NULL, NULL);
                ang_api->list_push(list, ang_api->string(arg));
                free(arg);
            }
        }
        LocalFree(wargv);
    }
#elif defined(__APPLE__)
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
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (f) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), f)) {
        }
        fclose(f);
    }
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

AngaraObject Angara_env_pid(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_i64((int64_t)GetCurrentProcessId());
#else
    return ang_i64((int64_t)getpid());
#endif
}

AngaraObject Angara_env_ppid(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    // Windows: use Toolhelp32 to get parent process ID
    DWORD ppid = 0;
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = {0};
        pe.dwSize = sizeof(pe);
        DWORD pid = GetCurrentProcessId();
        if (Process32First(h, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    ppid = pe.th32ParentProcessID;
                    break;
                }
            } while (Process32Next(h, &pe));
        }
        CloseHandle(h);
    }
    return ang_i64((int64_t)ppid);
#else
    return ang_i64((int64_t)getppid());
#endif
}

AngaraObject Angara_env_cwd(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    wchar_t wbuf[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, wbuf)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            char* buf = (char*)malloc(len);
            WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, len, NULL, NULL);
            AngaraObject result = ang_api->string(buf);
            free(buf);
            return result;
        }
    }
#else
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        return ang_api->string(buf);
    }
#endif
    ang_api->throw_error("env.cwd: failed to get current directory.");
    return ang_nil();
}

AngaraObject Angara_env_chdir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("env.chdir(path) expects one string argument.");
        return ang_nil();
    }
#ifdef _WIN32
    if (!SetCurrentDirectoryA(ang_api->as_cstr(args[0]))) {
        ang_api->throw_error("env.chdir: failed to change directory.");
    }
#else
    if (chdir(ang_api->as_cstr(args[0])) != 0) {
        ang_api->throw_error("env.chdir: failed to change directory.");
    }
#endif
    return ang_nil();
}

AngaraObject Angara_env_hostname(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    char buf[256];
#ifdef _WIN32
    DWORD size = sizeof(buf);
    if (GetComputerNameA(buf, &size)) {
        buf[sizeof(buf) - 1] = '\0';
        return ang_api->string(buf);
    }
#else
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return ang_api->string(buf);
    }
#endif
    ang_api->throw_error("env.hostname: failed to get hostname.");
    return ang_nil();
}

AngaraObject Angara_env_user(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    const char* user = getenv("USER");
    if (!user) user = getenv("LOGNAME");
    if (!user) user = getenv("USERNAME");  // Windows: USERNAME is the standard env var
    if (user) return ang_api->string(user);
    return ang_api->string("unknown");
}

AngaraObject Angara_env_home(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");  // Windows: USERPROFILE is more common
#endif
    if (home) return ang_api->string(home);
    return ang_api->string("/tmp");
}

AngaraObject Angara_env_shell(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    const char* shell = getenv("SHELL");
#ifdef _WIN32
    if (!shell) shell = getenv("COMSPEC");  // Windows: COMSPEC points to cmd.exe
    if (!shell) shell = "cmd.exe";
#else
    if (!shell) shell = "/bin/sh";
#endif
    return ang_api->string(shell);
}

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
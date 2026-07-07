/// Angara sys module — hostname, OS info, process info, memory stats. Links -lproc on macOS.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <sysinfoapi.h>
#else
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/utsname.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <libproc.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif
#endif

#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

AngaraObject Angara_sys_hostname(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    char buf[256];
    DWORD size = sizeof(buf);
    if (GetComputerNameA(buf, &size))
        return ang_api->string(buf);
    return ang_api->string("unknown");
#else
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) return ang_api->string("unknown");
    return ang_api->string(buf);
#endif
}

AngaraObject Angara_sys_os_name(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_api->string("Windows");
#else
    struct utsname info;
    if (uname(&info) != 0) return ang_api->string("unknown");
    return ang_api->string(info.sysname);
#endif
}

AngaraObject Angara_sys_os_version(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    OSVERSIONINFOA vi = {0};
    vi.dwOSVersionInfoSize = sizeof(vi);
    // Prefer RtlGetVersion if available; fallback to GetVersionExA
    char buf[64];
    snprintf(buf, sizeof(buf), "%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion);
    return ang_api->string(buf);
#else
    struct utsname info;
    if (uname(&info) != 0) return ang_api->string("unknown");
    return ang_api->string(info.release);
#endif
}

AngaraObject Angara_sys_arch(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: return ang_api->string("x86_64");
    case PROCESSOR_ARCHITECTURE_ARM64: return ang_api->string("aarch64");
    case PROCESSOR_ARCHITECTURE_INTEL: return ang_api->string("x86");
    default: return ang_api->string("unknown");
    }
#else
    struct utsname info;
    if (uname(&info) != 0) return ang_api->string("unknown");
    return ang_api->string(info.machine);
#endif
}

AngaraObject Angara_sys_pid(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_i64((int64_t)GetCurrentProcessId());
#else
    return ang_i64((int64_t)getpid());
#endif
}

AngaraObject Angara_sys_ppid(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    DWORD ppid = 0;
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = {0};
        pe.dwSize = sizeof(pe);
        DWORD pid = GetCurrentProcessId();
        if (Process32First(h, &pe)) {
            do {
                if (pe.th32ProcessID == pid) { ppid = pe.th32ParentProcessID; break; }
            } while (Process32Next(h, &pe));
        }
        CloseHandle(h);
    }
    return ang_i64((int64_t)ppid);
#else
    return ang_i64((int64_t)getppid());
#endif
}

AngaraObject Angara_sys_uid(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_i64(0);  // Windows: no meaningful uid for most use cases
#else
    return ang_i64((int64_t)getuid());
#endif
}

AngaraObject Angara_sys_gid(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_i64(0);  // Windows: no meaningful gid
#else
    return ang_i64((int64_t)getgid());
#endif
}

AngaraObject Angara_sys_cwd(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    char buf[MAX_PATH];
    if (GetCurrentDirectoryA(sizeof(buf), buf))
        return ang_api->string(buf);
#else
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        ang_api->throw_error("sys.cwd: failed to get current directory.");
        return ang_nil();
    }
    return ang_api->string(buf);
#endif
    ang_api->throw_error("sys.cwd: failed to get current directory.");
    return ang_nil();
}

AngaraObject Angara_sys_chdir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("sys.chdir(path) expects one string argument.");
        return ang_nil();
    }
#ifdef _WIN32
    if (!SetCurrentDirectoryA(ang_api->as_cstr(args[0]))) {
        ang_api->throw_error("sys.chdir: failed to change directory.");
    }
#else
    if (chdir(ang_api->as_cstr(args[0])) != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "sys.chdir: %s", strerror(errno));
        ang_api->throw_error(buf);
    }
#endif
    return ang_nil();
}

AngaraObject Angara_sys_setenv(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("sys.setenv(name, value) expects two string arguments.");
        return ang_nil();
    }
#ifdef _WIN32
    if (_putenv_s(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1])) != 0) {
        ang_api->throw_error("sys.setenv: failed to set environment variable.");
    }
#else
    if (setenv(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1]), 1) != 0) {
        ang_api->throw_error("sys.setenv: failed to set environment variable.");
    }
#endif
    return ang_nil();
}

AngaraObject Angara_sys_unsetenv(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("sys.unsetenv(name) expects one string argument.");
        return ang_nil();
    }
#ifdef _WIN32
    char buf[512];
    snprintf(buf, sizeof(buf), "%s=", ang_api->as_cstr(args[0]));
    _putenv(buf);
#else
    unsetenv(ang_api->as_cstr(args[0]));
#endif
    return ang_nil();
}

AngaraObject Angara_sys_getenv(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("sys.getenv(name) expects one string argument.");
        return ang_nil();
    }
    const char* val = getenv(ang_api->as_cstr(args[0]));
    if (!val) return ang_nil();
    return ang_api->string(val);
}

AngaraObject Angara_sys_getenv_all(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    AngaraObject rec = ang_api->record_new();
#ifdef _WIN32
    LPWCH env_block = GetEnvironmentStringsW();
    if (env_block) {
        LPWCH p = env_block;
        while (*p) {
            int len = WideCharToMultiByte(CP_UTF8, 0, p, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                char* entry = (char*)malloc(len);
                WideCharToMultiByte(CP_UTF8, 0, p, -1, entry, len, NULL, NULL);
                char* eq = strchr(entry, '=');
                if (eq) {
                    size_t key_len = (size_t)(eq - entry);
                    char* key = (char*)malloc(key_len + 1);
                    memcpy(key, entry, key_len);
                    key[key_len] = '\0';
                    ang_api->record_set(rec, key, ang_api->string(eq + 1));
                    free(key);
                }
                free(entry);
            }
            p += wcslen(p) + 1;
        }
        FreeEnvironmentStringsW(env_block);
    }
#else
    extern char** environ;
    for (char** env = environ; *env != NULL; env++) {
        const char* entry = *env;
        const char* eq = strchr(entry, '=');
        if (eq) {
            size_t key_len = (size_t)(eq - entry);
            char* key = (char*)malloc(key_len + 1);
            memcpy(key, entry, key_len);
            key[key_len] = '\0';
            ang_api->record_set(rec, key, ang_api->string(eq + 1));
            free(key);
        }
    }
#endif
    return rec;
}

AngaraObject Angara_sys_cpu_count(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return ang_i64((int64_t)si.dwNumberOfProcessors);
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count <= 0) return ang_i64(1);
    return ang_i64((int64_t)count);
#endif
}

AngaraObject Angara_sys_mem_info(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    AngaraObject rec = ang_api->record_new();

#ifdef _WIN32
    MEMORYSTATUSEX statex = {0};
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
        ang_api->record_set(rec, "total", ang_i64((int64_t)statex.ullTotalPhys));
        ang_api->record_set(rec, "free", ang_i64((int64_t)statex.ullAvailPhys));
        ang_api->record_set(rec, "used", ang_i64((int64_t)(statex.ullTotalPhys - statex.ullAvailPhys)));
        ang_api->record_set(rec, "available", ang_i64((int64_t)statex.ullAvailPhys));
    }
#elif defined(__APPLE__)
    int mib[2];
    int64_t physical_memory;
    size_t length = sizeof(int64_t);
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    if (sysctl(mib, 2, &physical_memory, &length, NULL, 0) == 0) {
        ang_api->record_set(rec, "total", ang_i64(physical_memory));
    } else {
        ang_api->record_set(rec, "total", ang_i64(0));
    }

    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
        int64_t page_size_bytes = (int64_t)vm_kernel_page_size;
        int64_t free_mem = (int64_t)vm_stats.free_count * page_size_bytes;
        int64_t active_mem = (int64_t)vm_stats.active_count * page_size_bytes;
        int64_t inactive_mem = (int64_t)vm_stats.inactive_count * page_size_bytes;
        int64_t used_mem = active_mem + inactive_mem;
        ang_api->record_set(rec, "free", ang_i64(free_mem));
        ang_api->record_set(rec, "used", ang_i64(used_mem));
        ang_api->record_set(rec, "available", ang_i64(free_mem + inactive_mem));
    }
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        int64_t total = (int64_t)info.totalram * (int64_t)info.mem_unit;
        int64_t free = (int64_t)info.freeram * (int64_t)info.mem_unit;
        int64_t used = total - free;
        ang_api->record_set(rec, "total", ang_i64(total));
        ang_api->record_set(rec, "free", ang_i64(free));
        ang_api->record_set(rec, "used", ang_i64(used));
        ang_api->record_set(rec, "available", ang_i64(free));
    }
#endif

    return rec;
}

AngaraObject Angara_sys_uptime(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_f64((double)GetTickCount64() / 1000.0);
#elif defined(__APPLE__)
    struct timeval boot_time;
    size_t size = sizeof(boot_time);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &boot_time, &size, NULL, 0) == 0) {
        struct timeval now;
        gettimeofday(&now, NULL);
        double uptime = (double)(now.tv_sec - boot_time.tv_sec) +
                        (double)(now.tv_usec - boot_time.tv_usec) / 1000000.0;
        return ang_f64(uptime);
    }
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return ang_f64((double)info.uptime);
    }
#endif
    return ang_f64(0.0);
}

AngaraObject Angara_sys_executable_path(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    char buf[MAX_PATH];
    if (GetModuleFileNameA(NULL, buf, sizeof(buf)))
        return ang_api->string(buf);
#elif defined(__APPLE__)
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(getpid(), buf, sizeof(buf)) > 0) {
        return ang_api->string(buf);
    }
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return ang_api->string(buf);
    }
#endif
    return ang_api->string("");
}

AngaraObject Angara_sys_user(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    const char* user = getenv("USER");
    if (!user) user = getenv("LOGNAME");
#ifdef _WIN32
    if (!user) user = getenv("USERNAME");
#endif
    if (!user) user = "unknown";
    return ang_api->string(user);
}

AngaraObject Angara_sys_shell(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    const char* shell = getenv("SHELL");
#ifdef _WIN32
    if (!shell) shell = getenv("COMSPEC");
    if (!shell) shell = "cmd.exe";
#else
    if (!shell) shell = "/bin/sh";
#endif
    return ang_api->string(shell);
}

AngaraObject Angara_sys_home(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    if (!home) home = "/";
    return ang_api->string(home);
}

AngaraObject Angara_sys_tmp(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    return ang_api->string(tmp);
}

AngaraObject Angara_sys_page_size(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return ang_i64((int64_t)si.dwPageSize);
#else
    return ang_i64((int64_t)sysconf(_SC_PAGESIZE));
#endif
}

AngaraObject Angara_sys_clock_ticks(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
#ifdef _WIN32
    return ang_i64(1000);  // Windows: GetTickCount64 uses millisecond ticks
#else
    return ang_i64((int64_t)sysconf(_SC_CLK_TCK));
#endif
}

static const AngaraFuncDef SYS_EXPORTS[] = {
    {"hostname",        Angara_sys_hostname,        "->s",    NULL},
    {"os_name",         Angara_sys_os_name,         "->s",    NULL},
    {"os_version",      Angara_sys_os_version,      "->s",    NULL},
    {"arch",            Angara_sys_arch,            "->s",    NULL},
    {"pid",             Angara_sys_pid,             "->i",    NULL},
    {"ppid",            Angara_sys_ppid,            "->i",    NULL},
    {"uid",             Angara_sys_uid,             "->i",    NULL},
    {"gid",             Angara_sys_gid,             "->i",    NULL},
    {"cwd",             Angara_sys_cwd,             "->s",    NULL},
    {"chdir",           Angara_sys_chdir,           "s->n",   NULL},
    {"setenv",          Angara_sys_setenv,          "ss->n",  NULL},
    {"unsetenv",        Angara_sys_unsetenv,        "s->n",   NULL},
    {"getenv",          Angara_sys_getenv,          "s->s",   NULL},
    {"getenv_all",      Angara_sys_getenv_all,      "->{}",   NULL},
    {"cpu_count",       Angara_sys_cpu_count,       "->i",    NULL},
    {"mem_info",        Angara_sys_mem_info,        "->{}",   NULL},
    {"uptime",          Angara_sys_uptime,          "->d",    NULL},
    {"executable_path",  Angara_sys_executable_path, "->s",   NULL},
    {"user",            Angara_sys_user,            "->s",    NULL},
    {"shell",           Angara_sys_shell,           "->s",    NULL},
    {"home",            Angara_sys_home,            "->s",    NULL},
    {"tmp",             Angara_sys_tmp,             "->s",    NULL},
    {"page_size",       Angara_sys_page_size,       "->i",    NULL},
    {"clock_ticks",     Angara_sys_clock_ticks,     "->i",    NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(sys) {
    ang_api = api;
    *def_count = (sizeof(SYS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return SYS_EXPORTS;
}
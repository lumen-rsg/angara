/// Angara unistd module — low-level POSIX file descriptors, fork/exec, pipes, signals.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

AngaraObject Angara_unistd_fork(int arg_count, AngaraObject* args) {
    pid_t pid = fork();
    if (pid < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "fork() failed: %s", strerror(errno));
        ang_api->throw_error(buf);
        return ang_i64(-1);
    }
    return ang_i64((int64_t)pid);
}

AngaraObject Angara_unistd_exec(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("exec(path, args...) expects at least a string path.");
        return ang_nil();
    }

    const char* path = ang_api->as_cstr(args[0]);

    char** argv = (char**)malloc((arg_count + 1) * sizeof(char*));
    if (!argv) { ang_api->throw_error("exec: out of memory."); return ang_nil(); }

    argv[0] = (char*)ang_api->as_cstr(args[0]);
    for (int i = 1; i < arg_count; i++) {
        if (IS_STR(args[i])) {
            argv[i] = (char*)ang_api->as_cstr(args[i]);
        } else {
            argv[i] = (char*)"";
        }
    }
    argv[arg_count] = NULL;

    execvp(path, argv);

    char buf[256];
    snprintf(buf, sizeof(buf), "exec(\"%s\") failed: %s", path, strerror(errno));
    free(argv);
    ang_api->throw_error(buf);
    return ang_nil();
}

AngaraObject Angara_unistd_waitpid(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.waitpid: expected 1 argument"); return ang_nil(); }
    pid_t pid = (pid_t)ang_as_i64(args[0]);
    int status = 0;
    pid_t result = waitpid(pid, &status, 0);
    if (result < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "waitpid(%d) failed: %s", (int)pid, strerror(errno));
        ang_api->throw_error(buf);
        return ang_nil();
    }
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int signaled = WIFSIGNALED(status) ? WTERMSIG(status) : 0;

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "pid", ang_i64((int64_t)result));
    ang_api->record_set(rec, "exit_code", ang_i64((int64_t)exit_code));
    if (signaled) {
        ang_api->record_set(rec, "signal", ang_i64((int64_t)signaled));
    }
    return rec;
}

AngaraObject Angara_unistd_getpid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getpid());
}

AngaraObject Angara_unistd_getppid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getppid());
}

AngaraObject Angara_unistd_getuid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getuid());
}

AngaraObject Angara_unistd_geteuid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)geteuid());
}

AngaraObject Angara_unistd_getgid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getgid());
}

AngaraObject Angara_unistd_getegid(int arg_count, AngaraObject* args) {
    return ang_i64((int64_t)getegid());
}

AngaraObject Angara_unistd_kill(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("unistd.kill: expected 2 arguments"); return ang_nil(); }
    pid_t pid = (pid_t)ang_as_i64(args[0]);
    int sig = (int)ang_as_i64(args[1]);
    if (kill(pid, sig) != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "kill(%d, %d) failed: %s", (int)pid, sig, strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_unistd_pipe(int arg_count, AngaraObject* args) {
    int fds[2];
    if (pipe(fds) != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "pipe() failed: %s", strerror(errno));
        ang_api->throw_error(buf);
        return ang_nil();
    }
    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "read_fd", ang_i64(fds[0]));
    ang_api->record_set(rec, "write_fd", ang_i64(fds[1]));
    return rec;
}

AngaraObject Angara_unistd_dup(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.dup: expected 1 argument"); return ang_nil(); }
    int fd = (int)ang_as_i64(args[0]);
    int newfd = dup(fd);
    if (newfd < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "dup(%d) failed: %s", fd, strerror(errno));
        ang_api->throw_error(buf);
        return ang_i64(-1);
    }
    return ang_i64((int64_t)newfd);
}

AngaraObject Angara_unistd_dup2(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("unistd.dup2: expected 2 arguments"); return ang_nil(); }
    int oldfd = (int)ang_as_i64(args[0]);
    int newfd = (int)ang_as_i64(args[1]);
    int result = dup2(oldfd, newfd);
    if (result < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "dup2(%d, %d) failed: %s", oldfd, newfd, strerror(errno));
        ang_api->throw_error(buf);
        return ang_i64(-1);
    }
    return ang_i64((int64_t)result);
}

AngaraObject Angara_unistd_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.close: expected 1 argument"); return ang_nil(); }
    int fd = (int)ang_as_i64(args[0]);
    if (close(fd) != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "close(%d) failed: %s", fd, strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_unistd_read(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("unistd.read: expected 2 arguments"); return ang_nil(); }
    int fd = (int)ang_as_i64(args[0]);
    size_t n = (size_t)ang_as_i64(args[1]);
    if (n == 0) return ang_api->string("");

    char* buf = (char*)malloc(n);
    if (!buf) { ang_api->throw_error("read: out of memory."); return ang_nil(); }

    ssize_t bytes_read = read(fd, buf, n);
    if (bytes_read < 0) {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "read(%d) failed: %s", fd, strerror(errno));
        free(buf);
        ang_api->throw_error(errbuf);
        return ang_nil();
    }
    if (bytes_read == 0) {
        free(buf);
        return ang_nil();
    }
    return ang_api->string_no_copy(buf, (size_t)bytes_read);
}

AngaraObject Angara_unistd_write(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("unistd.write: expected 2 arguments"); return ang_nil(); }
    int fd = (int)ang_as_i64(args[0]);
    const char* data = ang_api->as_cstr(args[1]);
    size_t len = ang_api->str_len(args[1]);

    ssize_t written = write(fd, data, len);
    if (written < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "write(%d) failed: %s", fd, strerror(errno));
        ang_api->throw_error(buf);
        return ang_i64(-1);
    }
    return ang_i64((int64_t)written);
}

AngaraObject Angara_unistd_lseek(int arg_count, AngaraObject* args) {
    if (arg_count < 3) { ang_api->throw_error("unistd.lseek: expected 3 arguments"); return ang_nil(); }
    int fd = (int)ang_as_i64(args[0]);
    off_t offset = (off_t)ang_as_i64(args[1]);
    int whence = (int)ang_as_i64(args[2]);
    off_t result = lseek(fd, offset, whence);
    if (result == (off_t)-1) {
        char buf[128];
        snprintf(buf, sizeof(buf), "lseek(%d) failed: %s", fd, strerror(errno));
        ang_api->throw_error(buf);
        return ang_i64(-1);
    }
    return ang_i64((int64_t)result);
}

AngaraObject Angara_unistd_isatty(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.isatty: expected 1 argument"); return ang_nil(); }
    int fd = (int)ang_as_i64(args[0]);
    return ang_bool(isatty(fd) == 1);
}

AngaraObject Angara_unistd_ttyname(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.ttyname: expected 1 argument"); return ang_nil(); }
    int fd = (int)ang_as_i64(args[0]);
    char* name = ttyname(fd);
    if (!name) return ang_nil();
    return ang_api->string(name);
}

AngaraObject Angara_unistd_unlink(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.unlink: expected 1 argument"); return ang_nil(); }
    if (unlink(ang_api->as_cstr(args[0])) != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "unlink(\"%s\") failed: %s", ang_api->as_cstr(args[0]), strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_unistd_symlink(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("unistd.symlink: expected 2 arguments"); return ang_nil(); }
    if (symlink(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1])) != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "symlink() failed: %s", strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_unistd_readlink(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.readlink: expected 1 argument"); return ang_nil(); }
    char buf[4096];
    ssize_t len = readlink(ang_api->as_cstr(args[0]), buf, sizeof(buf) - 1);
    if (len < 0) return ang_nil();
    buf[len] = '\0';
    return ang_api->string(buf);
}

AngaraObject Angara_unistd_access(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("unistd.access: expected 2 arguments"); return ang_nil(); }
    int mode = (int)ang_as_i64(args[1]);
    int result = access(ang_api->as_cstr(args[0]), mode);
    return ang_bool(result == 0);
}

AngaraObject Angara_unistd_chmod(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("unistd.chmod: expected 2 arguments"); return ang_nil(); }
    if (chmod(ang_api->as_cstr(args[0]), (mode_t)ang_as_i64(args[1])) != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "chmod() failed: %s", strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_unistd_chown(int arg_count, AngaraObject* args) {
    if (arg_count < 3) { ang_api->throw_error("unistd.chown: expected 3 arguments"); return ang_nil(); }
    if (chown(ang_api->as_cstr(args[0]), (uid_t)ang_as_i64(args[1]), (gid_t)ang_as_i64(args[2])) != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "chown() failed: %s", strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_unistd_STDIN(int arg_count, AngaraObject* args) {
    return ang_i64(STDIN_FILENO);
}

AngaraObject Angara_unistd_STDOUT(int arg_count, AngaraObject* args) {
    return ang_i64(STDOUT_FILENO);
}

AngaraObject Angara_unistd_STDERR(int arg_count, AngaraObject* args) {
    return ang_i64(STDERR_FILENO);
}

AngaraObject Angara_unistd_F_OK(int arg_count, AngaraObject* args) {
    return ang_i64(F_OK);
}

AngaraObject Angara_unistd_R_OK(int arg_count, AngaraObject* args) {
    return ang_i64(R_OK);
}

AngaraObject Angara_unistd_W_OK(int arg_count, AngaraObject* args) {
    return ang_i64(W_OK);
}

AngaraObject Angara_unistd_X_OK(int arg_count, AngaraObject* args) {
    return ang_i64(X_OK);
}

AngaraObject Angara_unistd_SEEK_SET(int arg_count, AngaraObject* args) {
    return ang_i64(SEEK_SET);
}

AngaraObject Angara_unistd_SEEK_CUR(int arg_count, AngaraObject* args) {
    return ang_i64(SEEK_CUR);
}

AngaraObject Angara_unistd_SEEK_END(int arg_count, AngaraObject* args) {
    return ang_i64(SEEK_END);
}

AngaraObject Angara_unistd_usleep(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.usleep: expected 1 argument"); return ang_nil(); }
    useconds_t usec = (useconds_t)ang_as_i64(args[0]);
    usleep(usec);
    return ang_nil();
}

AngaraObject Angara_unistd_alarm(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.alarm: expected 1 argument"); return ang_nil(); }
    unsigned int prev = alarm((unsigned int)ang_as_i64(args[0]));
    return ang_i64((int64_t)prev);
}

AngaraObject Angara_unistd_sysconf(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.sysconf: expected 1 argument"); return ang_nil(); }
    long val = sysconf((int)ang_as_i64(args[0]));
    if (val < 0) return ang_i64(-1);
    return ang_i64((int64_t)val);
}

AngaraObject Angara_unistd_getcwd(int arg_count, AngaraObject* args) {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        return ang_api->string(buf);
    }
    ang_api->throw_error("getcwd() failed.");
    return ang_nil();
}

AngaraObject Angara_unistd_chdir(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("unistd.chdir: expected 1 argument"); return ang_nil(); }
    if (chdir(ang_api->as_cstr(args[0])) != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "chdir() failed: %s", strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_unistd_hostname(int arg_count, AngaraObject* args) {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return ang_api->string(buf);
    }
    ang_api->throw_error("hostname() failed.");
    return ang_nil();
}

static const AngaraFuncDef UNISTD_EXPORTS[] = {
    {"fork",     Angara_unistd_fork,     "->i",     NULL},
    {"exec",     Angara_unistd_exec,     "s...->n", NULL},
    {"waitpid",  Angara_unistd_waitpid,  "i->{}",   NULL},
    {"getpid",   Angara_unistd_getpid,   "->i",     NULL},
    {"getppid",  Angara_unistd_getppid,  "->i",     NULL},
    {"getuid",   Angara_unistd_getuid,   "->i",     NULL},
    {"geteuid",  Angara_unistd_geteuid,  "->i",     NULL},
    {"getgid",   Angara_unistd_getgid,   "->i",     NULL},
    {"getegid",  Angara_unistd_getegid,  "->i",     NULL},
    {"kill",     Angara_unistd_kill,     "ii->n",   NULL},

    {"pipe",     Angara_unistd_pipe,     "->{}",    NULL},
    {"dup",      Angara_unistd_dup,      "i->i",    NULL},
    {"dup2",     Angara_unistd_dup2,     "ii->i",   NULL},
    {"close",    Angara_unistd_close,    "i->n",    NULL},
    {"read",     Angara_unistd_read,     "ii->s?",  NULL},
    {"write",    Angara_unistd_write,    "is->i",   NULL},
    {"lseek",    Angara_unistd_lseek,    "iii->i",  NULL},
    {"isatty",   Angara_unistd_isatty,   "i->b",    NULL},
    {"ttyname",  Angara_unistd_ttyname,  "i->s?",   NULL},

    {"unlink",   Angara_unistd_unlink,   "s->n",    NULL},
    {"symlink",  Angara_unistd_symlink,  "ss->n",   NULL},
    {"readlink", Angara_unistd_readlink, "s->s?",   NULL},
    {"access",   Angara_unistd_access,   "si->b",   NULL},
    {"chmod",    Angara_unistd_chmod,    "si->n",   NULL},
    {"chown",    Angara_unistd_chown,    "sii->n",  NULL},

    {"STDIN",    Angara_unistd_STDIN,    "->i",     NULL},
    {"STDOUT",   Angara_unistd_STDOUT,   "->i",     NULL},
    {"STDERR",   Angara_unistd_STDERR,   "->i",     NULL},
    {"F_OK",     Angara_unistd_F_OK,     "->i",     NULL},
    {"R_OK",     Angara_unistd_R_OK,     "->i",     NULL},
    {"W_OK",     Angara_unistd_W_OK,     "->i",     NULL},
    {"X_OK",     Angara_unistd_X_OK,     "->i",     NULL},
    {"SEEK_SET", Angara_unistd_SEEK_SET, "->i",     NULL},
    {"SEEK_CUR", Angara_unistd_SEEK_CUR, "->i",     NULL},
    {"SEEK_END", Angara_unistd_SEEK_END, "->i",     NULL},

    {"usleep",   Angara_unistd_usleep,   "i->n",    NULL},
    {"alarm",    Angara_unistd_alarm,    "i->i",    NULL},
    {"sysconf",  Angara_unistd_sysconf,  "i->i",    NULL},
    {"getcwd",   Angara_unistd_getcwd,   "->s",     NULL},
    {"chdir",    Angara_unistd_chdir,    "s->n",    NULL},
    {"hostname", Angara_unistd_hostname, "->s",     NULL},

    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(unistd) {
    ang_api = api;
    *def_count = (sizeof(UNISTD_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return UNISTD_EXPORTS;
}
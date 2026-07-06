/// Angara file-watching module — inotify (Linux) / kqueue (macOS, BSD).
///
/// Usage:
///   let w = watch.new()
///   let wd = w.add("/tmp", watch.CREATE | watch.DELETE)
///   for (ev in w.poll(1000)) { io.println(ev.name) }
///   w.close()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

/* =========================================================================
   Event mask constants — same values on all platforms.
   On BSD/macOS these are translated to kqueue fflags internally.
   ========================================================================= */

#ifdef __linux__
#include <sys/inotify.h>
#define WATCH_ACCESS        IN_ACCESS
#define WATCH_MODIFY        IN_MODIFY
#define WATCH_ATTRIB        IN_ATTRIB
#define WATCH_CLOSE_WRITE   IN_CLOSE_WRITE
#define WATCH_CLOSE_NOWRITE IN_CLOSE_NOWRITE
#define WATCH_OPEN          IN_OPEN
#define WATCH_MOVED_FROM    IN_MOVED_FROM
#define WATCH_MOVED_TO      IN_MOVED_TO
#define WATCH_CREATE        IN_CREATE
#define WATCH_DELETE        IN_DELETE
#define WATCH_DELETE_SELF   IN_DELETE_SELF
#define WATCH_MOVE_SELF     IN_MOVE_SELF
#define WATCH_ONLYDIR       IN_ONLYDIR
#define WATCH_DONT_FOLLOW   IN_DONT_FOLLOW
#define WATCH_EXCL_UNLINK   IN_EXCL_UNLINK
#define WATCH_MASK_CREATE   IN_MASK_CREATE
#define WATCH_ALL_EVENTS    IN_ALL_EVENTS
#else
/* kqueue backend — synthetic constants matching the inotify values so
   Angara user code is portable.  Internal translation happens in add(). */
#define WATCH_ACCESS        (1 << 0)
#define WATCH_MODIFY        (1 << 1)
#define WATCH_ATTRIB        (1 << 2)
#define WATCH_CLOSE_WRITE   (1 << 3)
#define WATCH_CLOSE_NOWRITE (1 << 4)
#define WATCH_OPEN          (1 << 5)
#define WATCH_MOVED_FROM    (1 << 6)
#define WATCH_MOVED_TO      (1 << 7)
#define WATCH_CREATE        (1 << 8)
#define WATCH_DELETE        (1 << 9)
#define WATCH_DELETE_SELF   (1 << 10)
#define WATCH_MOVE_SELF     (1 << 11)
#define WATCH_ONLYDIR       (1 << 12)
#define WATCH_DONT_FOLLOW   (1 << 13)
#define WATCH_EXCL_UNLINK   (1 << 14)
#define WATCH_MASK_CREATE   (1 << 15)
#define WATCH_ALL_EVENTS    0xFFFFFFFF
#endif

/* ---- mask-constant accessors ---- */
#define DEF_MASK(name) \
    AngaraObject Angara_watch_##name(int c, AngaraObject* a) { \
        (void)c; (void)a; return ang_i64(WATCH_##name); \
    }
DEF_MASK(ACCESS)
DEF_MASK(MODIFY)
DEF_MASK(ATTRIB)
DEF_MASK(CLOSE_WRITE)
DEF_MASK(CLOSE_NOWRITE)
DEF_MASK(OPEN)
DEF_MASK(MOVED_FROM)
DEF_MASK(MOVED_TO)
DEF_MASK(CREATE)
DEF_MASK(DELETE)
DEF_MASK(DELETE_SELF)
DEF_MASK(MOVE_SELF)
DEF_MASK(ONLYDIR)
DEF_MASK(DONT_FOLLOW)
DEF_MASK(EXCL_UNLINK)
DEF_MASK(MASK_CREATE)
DEF_MASK(ALL_EVENTS)
#undef DEF_MASK


/* =========================================================================
   Linux backend — inotify
   ========================================================================= */
#ifdef __linux__

#include <sys/inotify.h>

typedef struct {
    int fd;
} WatcherData;

static void finalize_watcher(void* data) {
    WatcherData* w = (WatcherData*)data;
    if (w->fd >= 0) close(w->fd);
    free(w);
}

AngaraObject Angara_watch_new(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        ang_api->throw_error("watch.new: inotify_init1 failed.");
        return ang_nil();
    }
    WatcherData* w = (WatcherData*)calloc(1, sizeof(WatcherData));
    w->fd = fd;
    return ang_api->native_instance_new(w, finalize_watcher, "Watcher");
}

AngaraObject Angara_Watcher_add(int arg_count, AngaraObject* args) {
    if (arg_count < 3 || !IS_STR(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("watcher.add(path, mask) expects a string and an i64.");
        return ang_nil();
    }
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (!w || w->fd < 0) return ang_i64(-1);

    const char* path = ang_api->as_cstr(args[1]);
    uint32_t mask = (uint32_t)ang_as_i64(args[2]);

    int wd = inotify_add_watch(w->fd, path, mask);
    if (wd < 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "watcher.add: failed to watch '%s': %s", path, strerror(errno));
        ang_api->throw_error(buf);
        return ang_nil();
    }
    return ang_i64(wd);
}

AngaraObject Angara_Watcher_remove(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_i64(args[1])) {
        ang_api->throw_error("watcher.remove(wd) expects an i64 watch descriptor.");
        return ang_nil();
    }
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (!w || w->fd < 0) return ang_nil();

    int wd = (int)ang_as_i64(args[1]);
    if (inotify_rm_watch(w->fd, wd) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "watcher.remove: failed to remove wd %d: %s", wd, strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_Watcher_poll(int arg_count, AngaraObject* args) {
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (!w || w->fd < 0) return ang_api->list_new();

    int timeout_ms = -1;
    if (arg_count >= 2 && ang_is_i64(args[1]))
        timeout_ms = (int)ang_as_i64(args[1]);

    struct pollfd pfd;
    pfd.fd = w->fd;
    pfd.events = POLLIN;
    int pret = poll(&pfd, 1, timeout_ms);
    if (pret <= 0) return ang_api->list_new();

    char ev_buf[4096];
    ssize_t n = read(w->fd, ev_buf, sizeof(ev_buf));
    if (n <= 0) return ang_api->list_new();

    AngaraObject list = ang_api->list_new();
    ssize_t i = 0;
    while (i < n) {
        struct inotify_event* ev = (struct inotify_event*)(ev_buf + i);
        AngaraObject rec = ang_api->record_new();
        ang_api->record_set(rec, "wd",     ang_i64(ev->wd));
        ang_api->record_set(rec, "mask",   ang_i64(ev->mask));
        ang_api->record_set(rec, "cookie", ang_i64(ev->cookie));
        if (ev->len > 0) {
            ang_api->record_set(rec, "name", ang_api->string(ev->name));
        }
        ang_api->list_push(list, rec);
        ang_api->decref(rec);
        i += (ssize_t)(sizeof(struct inotify_event) + ev->len);
    }
    return list;
}

AngaraObject Angara_Watcher_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Watcher.close: expected 1 argument"); return ang_nil(); }
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (w && w->fd >= 0) {
        close(w->fd);
        w->fd = -1;
    }
    return ang_nil();
}

/* =========================================================================
   BSD / macOS backend — kqueue  (UNTESTED — no macOS/BSD CI available)
   ========================================================================= */
#else

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

/* translate our synthetic mask bits to kqueue EVFILT_VNODE fflags */
static uint32_t mask_to_fflags(uint32_t mask) {
    uint32_t out = 0;
    if (mask & WATCH_DELETE)      out |= NOTE_DELETE;
    if (mask & WATCH_WRITE)       out |= NOTE_WRITE;
    if (mask & WATCH_EXTEND)      out |= NOTE_EXTEND;      /* file grew */
    if (mask & WATCH_ATTRIB)      out |= NOTE_ATTRIB;
    if (mask & WATCH_LINK)        out |= NOTE_LINK;
    if (mask & WATCH_RENAME)      out |= NOTE_RENAME;
    if (mask & WATCH_REVOKE)      out |= NOTE_REVOKE;
    /* CREATE / MOVED_TO / CLOSE_WRITE are approximated by NOTE_WRITE */
    if (mask & (WATCH_CREATE | WATCH_MOVED_TO | WATCH_CLOSE_WRITE | WATCH_MODIFY))
        out |= NOTE_WRITE;
    /* MOVED_FROM / CLOSE_NOWRITE / DELETE_SELF / MOVE_SELF → NOTE_DELETE */
    if (mask & (WATCH_MOVED_FROM | WATCH_CLOSE_NOWRITE | WATCH_OPEN))
        out |= NOTE_DELETE;  /* best-effort approximation */
    if (mask == WATCH_ALL_EVENTS)
        out = NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB |
              NOTE_LINK | NOTE_RENAME | NOTE_REVOKE;
    return out;
}

/* translate kqueue fflags back to our synthetic mask for poll() results */
static uint32_t fflags_to_mask(uint32_t fflags) {
    uint32_t out = 0;
    if (fflags & NOTE_DELETE) out |= WATCH_DELETE;
    if (fflags & NOTE_WRITE)  out |= WATCH_MODIFY;
    if (fflags & NOTE_EXTEND) out |= WATCH_MODIFY;
    if (fflags & NOTE_ATTRIB) out |= WATCH_ATTRIB;
    if (fflags & NOTE_LINK)   out |= WATCH_CREATE;
    if (fflags & NOTE_RENAME) out |= WATCH_MOVED_FROM;
    if (fflags & NOTE_REVOKE) out |= WATCH_DELETE_SELF;
    return out;
}

typedef struct {
    int   kq;          /* kqueue file descriptor */
    int*  wds;         /* watched file descriptors (returned as "wd") */
    char** paths;      /* paths for name lookup in poll results */
    size_t count;
    size_t cap;
} WatcherData;

static void finalize_watcher(void* data) {
    WatcherData* w = (WatcherData*)data;
    for (size_t i = 0; i < w->count; i++) {
        if (w->wds[i] >= 0) close(w->wds[i]);
        free(w->paths[i]);
    }
    free(w->wds);
    free(w->paths);
    if (w->kq >= 0) close(w->kq);
    free(w);
}

AngaraObject Angara_watch_new(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    int kq = kqueue();
    if (kq < 0) {
        ang_api->throw_error("watch.new: kqueue() failed.");
        return ang_nil();
    }
    WatcherData* w = (WatcherData*)calloc(1, sizeof(WatcherData));
    w->kq = kq;
    w->cap = 4;
    w->wds = (int*)malloc(w->cap * sizeof(int));
    w->paths = (char**)malloc(w->cap * sizeof(char*));
    return ang_api->native_instance_new(w, finalize_watcher, "Watcher");
}

AngaraObject Angara_Watcher_add(int arg_count, AngaraObject* args) {
    if (arg_count < 3 || !IS_STR(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("watcher.add(path, mask) expects a string and an i64.");
        return ang_nil();
    }
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (!w || w->kq < 0) return ang_i64(-1);

    const char* path = ang_api->as_cstr(args[1]);
    uint32_t mask = (uint32_t)ang_as_i64(args[2]);

    /* open the path to get a fd for kqueue monitoring */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "watcher.add: failed to open '%s': %s", path, strerror(errno));
        ang_api->throw_error(buf);
        return ang_nil();
    }

    uint32_t fflags = mask_to_fflags(mask);
    struct kevent kev;
    EV_SET(&kev, (uintptr_t)fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, NULL);

    if (kevent(w->kq, &kev, 1, NULL, 0, NULL) < 0) {
        close(fd);
        char buf[256];
        snprintf(buf, sizeof(buf), "watcher.add: kevent failed for '%s': %s", path, strerror(errno));
        ang_api->throw_error(buf);
        return ang_nil();
    }

    /* store fd + path for later lookup and cleanup */
    if (w->count >= w->cap) {
        w->cap *= 2;
        w->wds   = (int*)  realloc(w->wds,   w->cap * sizeof(int));
        w->paths = (char**)realloc(w->paths, w->cap * sizeof(char*));
    }
    w->wds[w->count]   = fd;
    w->paths[w->count] = strdup(path);
    w->count++;

    return ang_i64((int64_t)fd);  /* fd doubles as watch descriptor */
}

AngaraObject Angara_Watcher_remove(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_i64(args[1])) {
        ang_api->throw_error("watcher.remove(wd) expects an i64 watch descriptor.");
        return ang_nil();
    }
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (!w || w->kq < 0) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);

    /* remove from our tracking arrays */
    for (size_t i = 0; i < w->count; i++) {
        if (w->wds[i] == fd) {
            close(fd);
            free(w->paths[i]);
            w->wds[i]   = w->wds[w->count - 1];
            w->paths[i] = w->paths[w->count - 1];
            w->count--;
            return ang_nil();
        }
    }
    /* not found */
    return ang_nil();
}

AngaraObject Angara_Watcher_poll(int arg_count, AngaraObject* args) {
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (!w || w->kq < 0) return ang_api->list_new();

    int timeout_ms = -1;
    if (arg_count >= 2 && ang_is_i64(args[1]))
        timeout_ms = (int)ang_as_i64(args[1]);

    struct timespec ts;
    struct timespec* tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec  = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }

    struct kevent events[32];
    int n = kevent(w->kq, NULL, 0, events, 32, tsp);
    if (n <= 0) return ang_api->list_new();

    AngaraObject list = ang_api->list_new();
    for (int i = 0; i < n; i++) {
        int fd = (int)events[i].ident;

        /* look up the path for this fd */
        const char* name = NULL;
        for (size_t j = 0; j < w->count; j++) {
            if (w->wds[j] == fd) { name = w->paths[j]; break; }
        }

        AngaraObject rec = ang_api->record_new();
        ang_api->record_set(rec, "wd",     ang_i64(fd));
        ang_api->record_set(rec, "mask",   ang_i64((int64_t)fflags_to_mask((uint32_t)events[i].fflags)));
        ang_api->record_set(rec, "cookie", ang_i64(0));  /* kqueue has no cookie */
        if (name) {
            /* extract just the filename portion for consistency with inotify */
            const char* slash = strrchr(name, '/');
            ang_api->record_set(rec, "name", ang_api->string(slash ? slash + 1 : name));
        }
        ang_api->list_push(list, rec);
        ang_api->decref(rec);
    }
    return list;
}

AngaraObject Angara_Watcher_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Watcher.close: expected 1 argument"); return ang_nil(); }
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (w) {
        for (size_t i = 0; i < w->count; i++) {
            if (w->wds[i] >= 0) close(w->wds[i]);
            free(w->paths[i]);
        }
        free(w->wds);   w->wds   = NULL;
        free(w->paths); w->paths = NULL;
        w->count = 0;
        w->cap   = 0;
        if (w->kq >= 0) { close(w->kq); w->kq = -1; }
    }
    return ang_nil();
}

#endif  /* __linux__ */


/* =========================================================================
   Export table (common)
   ========================================================================= */

static const AngaraMethodDef WATCHER_METHODS[] = {
    {"add",    (AngaraMethodFn)Angara_Watcher_add,    "si->i"},
    {"remove", (AngaraMethodFn)Angara_Watcher_remove, "i->n"},
    {"poll",   (AngaraMethodFn)Angara_Watcher_poll,   "i?->l<{}>"},
    {"close",  (AngaraMethodFn)Angara_Watcher_close,  "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef WATCHER_CLASS = { "Watcher", NULL, WATCHER_METHODS };

static const AngaraFuncDef WATCH_EXPORTS[] = {
    {"new",       Angara_watch_new,       "->Watcher", &WATCHER_CLASS},

    {"ACCESS",        Angara_watch_ACCESS,        "->i", NULL},
    {"MODIFY",        Angara_watch_MODIFY,        "->i", NULL},
    {"ATTRIB",        Angara_watch_ATTRIB,        "->i", NULL},
    {"CLOSE_WRITE",   Angara_watch_CLOSE_WRITE,   "->i", NULL},
    {"CLOSE_NOWRITE", Angara_watch_CLOSE_NOWRITE, "->i", NULL},
    {"OPEN",          Angara_watch_OPEN,          "->i", NULL},
    {"MOVED_FROM",    Angara_watch_MOVED_FROM,    "->i", NULL},
    {"MOVED_TO",      Angara_watch_MOVED_TO,      "->i", NULL},
    {"CREATE",        Angara_watch_CREATE,        "->i", NULL},
    {"DELETE",        Angara_watch_DELETE,        "->i", NULL},
    {"DELETE_SELF",   Angara_watch_DELETE_SELF,   "->i", NULL},
    {"MOVE_SELF",     Angara_watch_MOVE_SELF,     "->i", NULL},
    {"ONLYDIR",       Angara_watch_ONLYDIR,       "->i", NULL},
    {"DONT_FOLLOW",   Angara_watch_DONT_FOLLOW,   "->i", NULL},
    {"EXCL_UNLINK",   Angara_watch_EXCL_UNLINK,   "->i", NULL},
    {"MASK_CREATE",   Angara_watch_MASK_CREATE,   "->i", NULL},
    {"ALL_EVENTS",    Angara_watch_ALL_EVENTS,     "->i", NULL},

    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(watch) {
    ang_api = api;
    *def_count = (sizeof(WATCH_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return WATCH_EXPORTS;
}

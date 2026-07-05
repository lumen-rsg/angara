/// Angara file-watching module — Linux inotify wrapper.
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
#include <poll.h>
#include <sys/inotify.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

/* ---- event mask constants ---- */
#define DEF_MASK(name) \
    AngaraObject Angara_watch_##name(int c, AngaraObject* a) { \
        (void)c; (void)a; return ang_i64(IN_##name); \
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
DEF_MASK(MASK_CREATE)    /* for legacy IN_MASK_CREATE */
#undef DEF_MASK

/* convenience combos */
AngaraObject Angara_watch_ALL_EVENTS(int c, AngaraObject* a) {
    (void)c; (void)a;
    return ang_i64(IN_ALL_EVENTS);
}


/* ---- Watcher native class ---- */

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
    /* args: self, path:string, mask:i64 */
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
    /* args: self, wd:i64 */
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
    /* args: self, timeout_ms:i64? */
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (!w || w->fd < 0) return ang_api->list_new();

    int timeout_ms = -1; /* block forever by default */
    if (arg_count >= 2 && ang_is_i64(args[1]))
        timeout_ms = (int)ang_as_i64(args[1]);

    /* poll for readability */
    struct pollfd pfd;
    pfd.fd = w->fd;
    pfd.events = POLLIN;
    int pret = poll(&pfd, 1, timeout_ms);
    if (pret < 0) return ang_api->list_new();
    if (pret == 0) return ang_api->list_new();  /* timeout, no events */

    /* read all available events */
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
    WatcherData* w = (WatcherData*)ang_api->native_instance_data(args[0]);
    if (w && w->fd >= 0) {
        close(w->fd);
        w->fd = -1;
    }
    return ang_nil();
}


/* ---- export table ---- */

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

    /* event mask constants */
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

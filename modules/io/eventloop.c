/// Angara async module — epoll event loop + thread-safe channels.
///
/// Supports both poll-driven and callback-driven usage:
///
///   let loop = async.Loop()
///   loop.on_readable(sock_fd, func() { ... })
///   loop.set_timeout(1000, func() { io.println(1, "tick") })
///   loop.run()   // blocks, dispatches callbacks
///
///   let (tx, rx) = async.channel(16)
///   tx.send("hello")
///   let msg = rx.recv()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdatomic.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

/* =========================================================================
   Event mask constants
   ========================================================================= */

AngaraObject Angara_eventloop_READ(int c, AngaraObject* a)  { (void)c;(void)a; return ang_i64(EPOLLIN); }
AngaraObject Angara_eventloop_WRITE(int c, AngaraObject* a) { (void)c;(void)a; return ang_i64(EPOLLOUT); }
AngaraObject Angara_eventloop_RDWR(int c, AngaraObject* a)  { (void)c;(void)a; return ang_i64(EPOLLIN | EPOLLOUT); }

AngaraObject Angara_eventloop_READABLE(int c, AngaraObject* a) { (void)c;(void)a; return ang_i64(1); }
AngaraObject Angara_eventloop_WRITABLE(int c, AngaraObject* a) { (void)c;(void)a; return ang_i64(2); }
AngaraObject Angara_eventloop_TIMER(int c, AngaraObject* a)    { (void)c;(void)a; return ang_i64(3); }
AngaraObject Angara_eventloop_CHANNEL(int c, AngaraObject* a)   { (void)c;(void)a; return ang_i64(4); }


/* =========================================================================
   Future frame — standard header layout (LIB-4 Stage S-2)
   Must match the LLVM codegen frame layout:
     { i32 state, AngaraObject result, AngaraObject awaited,
       ptr waker_fn, ptr waker_ctx, ptr loop }
   ========================================================================= */

typedef struct {
    int32_t       state;       // offset 0  (field 0: -1=resolved, >=0=pending)
    int32_t       _pad;        // offset 4  (alignment padding)
    AngaraObject  result;      // offset 8  (field 1: return value)
    AngaraObject  awaited;     // offset 24 (field 2: awaited future)
    void*         waker_fn;    // offset 40 (field 3: resume function)
    void*         waker_ctx;   // offset 48 (field 4: parent frame)
    void*         loop;        // offset 56 (field 5: owning event loop)
} FutureFrame;

/* Future kinds (LIB-4 I/O integration) */
#define FUTURE_KIND_TIMER  1
#define FUTURE_KIND_READ   2
#define FUTURE_KIND_WRITE  3
#define FUTURE_KIND_ACCEPT 4

/* Unified I/O future — timer, read, write, or accept.
   The FutureFrame header must be first (codegen accesses it by offset).
   For ACCEPT, no flex buffer is allocated (sizeof(IOFuture) only). */
typedef struct {
    FutureFrame   header;      // 64 bytes: standard layout
    int           kind;        // FUTURE_KIND_*
    int           fd;          // timerfd or I/O fd
    size_t        count;       // read: buffer size; write: total len
    size_t        offset;      // write: bytes written so far
    char          buf[];       // flexible array: read buffer or write data copy
} IOFuture;


/* =========================================================================
   Callback registry — maps fd → callback closure
   ========================================================================= */

typedef struct {
    int           fd;
    AngaraObject  callback;   /* Angara closure */
    int           is_timer;
    int           is_interval; /* 0 = one-shot, 1 = repeating */
    int           is_writable; /* 0 = EPOLLIN, 1 = EPOLLOUT */
} CbEntry;

typedef struct {
    CbEntry* entries;
    size_t   count;
    size_t   cap;
} CbRegistry;

static void cb_registry_init(CbRegistry* r) {
    r->cap = 8;
    r->count = 0;
    r->entries = (CbEntry*)calloc(r->cap, sizeof(CbEntry));
}

static void cb_registry_free(CbRegistry* r) {
    for (size_t i = 0; i < r->count; i++) {
        ang_api->decref(r->entries[i].callback);
    }
    free(r->entries);
    r->entries = NULL;
    r->count = r->cap = 0;
}

static CbEntry* cb_registry_find(CbRegistry* r, int fd) {
    for (size_t i = 0; i < r->count; i++) {
        if (r->entries[i].fd == fd) return &r->entries[i];
    }
    return NULL;
}

static CbEntry* cb_registry_add(CbRegistry* r, int fd, AngaraObject cb,
                                 int is_timer, int is_interval, int is_writable) {
    if (r->count >= r->cap) {
        size_t new_cap = r->cap * 2;
        CbEntry* new_entries = (CbEntry*)realloc(r->entries, new_cap * sizeof(CbEntry));
        if (!new_entries) return NULL;
        memset(new_entries + r->count, 0, (new_cap - r->count) * sizeof(CbEntry));
        r->entries = new_entries;
        r->cap = new_cap;
    }
    CbEntry* e = &r->entries[r->count++];
    e->fd = fd;
    e->callback = cb;
    ang_api->incref(cb);
    e->is_timer    = is_timer;
    e->is_interval = is_interval;
    e->is_writable = is_writable;
    return e;
}

static void cb_registry_remove(CbRegistry* r, int fd) {
    for (size_t i = 0; i < r->count; i++) {
        if (r->entries[i].fd == fd) {
            ang_api->decref(r->entries[i].callback);
            r->entries[i] = r->entries[--r->count];
            return;
        }
    }
}


/* =========================================================================
   Loop
   ========================================================================= */

typedef struct {
    int         epfd;
    int         stop_efd;     /* eventfd for loop.stop() wake-up */
    CbRegistry  cbs;          /* fd → callback */
    int         running;      /* 1 while loop.run() is active */
    /* Pending futures (timer + I/O) */
    IOFuture**  pending;      /* active futures array */
    size_t      pending_count;
    size_t      pending_cap;
} LoopData;

static void finalize_loop(void* data) {
    LoopData* l = (LoopData*)data;
    cb_registry_free(&l->cbs);
    if (l->pending) free(l->pending);
    if (l->stop_efd >= 0) close(l->stop_efd);
    if (l->epfd >= 0) close(l->epfd);
    free(l);
}

AngaraObject Angara_eventloop_Loop(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        ang_api->throw_error("async.Loop: epoll_create1 failed.");
        return ang_nil();
    }
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
        close(epfd);
        ang_api->throw_error("async.Loop: eventfd failed.");
        return ang_nil();
    }
    /* register stop_efd so run() can be woken up */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = efd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &ev);

    LoopData* l = (LoopData*)calloc(1, sizeof(LoopData));
    l->epfd = epfd;
    l->stop_efd = efd;
    cb_registry_init(&l->cbs);
    return ang_api->native_instance_new(l, finalize_loop, "Loop");
}

/* ---- callback registration ---- */

AngaraObject Angara_Loop_on_readable(int arg_count, AngaraObject* args) {
    /* loop.on_readable(fd, callback) */
    if (arg_count < 3 || !ang_is_i64(args[1])) {
        ang_api->throw_error("loop.on_readable(fd, callback) expects i64 and closure.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    AngaraObject cb = args[2];

    /* remove any existing callback for this fd */
    cb_registry_remove(&l->cbs, fd);

    /* register callback */
    cb_registry_add(&l->cbs, fd, cb, 0, 0, 0);

    /* add to epoll (or modify if already there) */
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev);
    return ang_nil();
}

AngaraObject Angara_Loop_on_writable(int arg_count, AngaraObject* args) {
    if (arg_count < 3 || !ang_is_i64(args[1])) {
        ang_api->throw_error("loop.on_writable(fd, callback) expects i64 and closure.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    AngaraObject cb = args[2];

    cb_registry_remove(&l->cbs, fd);
    cb_registry_add(&l->cbs, fd, cb, 0, 0, 1);

    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev);
    return ang_nil();
}

AngaraObject Angara_Loop_set_timeout(int arg_count, AngaraObject* args) {
    /* loop.set_timeout(ms, callback) — one-shot */
    if (arg_count < 3 || !ang_is_i64(args[1])) {
        ang_api->throw_error("loop.set_timeout(ms, callback) expects i64 and closure.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_i64(-1);

    int64_t ms = ang_as_i64(args[1]);
    if (ms < 1) ms = 1;
    AngaraObject cb = args[2];

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        ang_api->throw_error("loop.set_timeout: timerfd_create failed.");
        return ang_i64(-1);
    }

    struct itimerspec its;
    its.it_value.tv_sec     = ms / 1000;
    its.it_value.tv_nsec    = (ms % 1000) * 1000000L;
    its.it_interval.tv_sec  = 0;  /* one-shot */
    its.it_interval.tv_nsec = 0;
    timerfd_settime(tfd, 0, &its, NULL);

    cb_registry_add(&l->cbs, tfd, cb, 1, 0, 0);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tfd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, tfd, &ev);

    return ang_i64(tfd);
}

AngaraObject Angara_Loop_set_interval(int arg_count, AngaraObject* args) {
    /* loop.set_interval(ms, callback) — repeating */
    if (arg_count < 3 || !ang_is_i64(args[1])) {
        ang_api->throw_error("loop.set_interval(ms, callback) expects i64 and closure.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_i64(-1);

    int64_t ms = ang_as_i64(args[1]);
    if (ms < 1) ms = 1;
    AngaraObject cb = args[2];

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        ang_api->throw_error("loop.set_interval: timerfd_create failed.");
        return ang_i64(-1);
    }

    struct itimerspec its;
    its.it_value.tv_sec     = ms / 1000;
    its.it_value.tv_nsec    = (ms % 1000) * 1000000L;
    its.it_interval.tv_sec  = ms / 1000;   /* repeating */
    its.it_interval.tv_nsec = (ms % 1000) * 1000000L;
    timerfd_settime(tfd, 0, &its, NULL);

    cb_registry_add(&l->cbs, tfd, cb, 1, 1, 0);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tfd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, tfd, &ev);

    return ang_i64(tfd);
}

AngaraObject Angara_Loop_clear(int arg_count, AngaraObject* args) {
    /* loop.clear(fd) — remove fd/timer and its callback */
    if (arg_count < 2 || !ang_is_i64(args[1])) return ang_nil();
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    epoll_ctl(l->epfd, EPOLL_CTL_DEL, fd, NULL);
    cb_registry_remove(&l->cbs, fd);
    close(fd);
    return ang_nil();
}

/* ---- run / stop ---- */

AngaraObject Angara_Loop_run(int arg_count, AngaraObject* args) {
    /* loop.run(timeout_ms?) — blocks, dispatches callbacks.
       if (arg_count < 2) { ang_api->throw_error("Loop.run: expected 2 arguments"); return ang_nil(); }
       If timeout_ms is provided, returns after that many milliseconds.
       Otherwise blocks until loop.stop() is called. */
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_nil();

    int has_timeout = 0;
    int64_t timeout_ms = -1;
    if (arg_count >= 2 && ang_is_i64(args[1])) {
        timeout_ms = ang_as_i64(args[1]);
        has_timeout = 1;
    }

    l->running = 1;

    while (l->running) {
        int epoll_timeout = -1;
        if (has_timeout) epoll_timeout = (int)timeout_ms;

        struct epoll_event events[64];
        int n = epoll_wait(l->epfd, events, 64, epoll_timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* timeout elapsed */
        if (n == 0 && has_timeout) {
            l->running = 0;
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            /* stop_efd → exit the loop */
            if (fd == l->stop_efd) {
                uint64_t dummy;
                read(fd, &dummy, sizeof(dummy));
                l->running = 0;
                continue;
            }

            /* LIB-4: check if this fd belongs to a pending future */
            {
                IOFuture* fut = NULL;
                for (size_t pi = 0; pi < l->pending_count; pi++) {
                    if (l->pending[pi] && l->pending[pi]->fd == fd) {
                        fut = l->pending[pi];
                        break;
                    }
                }
                if (fut) {
                    int remove_fd = 1;
                    switch (fut->kind) {
                    case FUTURE_KIND_TIMER: {
                        uint64_t exp = 0;
                        read(fd, &exp, sizeof(exp));
                        fut->header.state = -1;
                        break;
                    }
                    case FUTURE_KIND_READ: {
                        ssize_t nr = read(fd, fut->buf, fut->count);
                        if (nr > 0) {
                            fut->header.result = ang_api->string_len(fut->buf, (size_t)nr);
                        } else {
                            fut->header.result = ang_api->string_len("", 0);
                        }
                        fut->header.state = -1;
                        break;
                    }
                    case FUTURE_KIND_WRITE: {
                        ssize_t nw = write(fd, fut->buf + fut->offset,
                                           fut->count - fut->offset);
                        if (nw > 0) {
                            fut->offset += (size_t)nw;
                        }
                        if (fut->offset >= fut->count) {
                            fut->header.result = ang_i64((int64_t)fut->count);
                            fut->header.state = -1;
                        } else {
                            /* Partial write — re-arm and keep waiting */
                            struct epoll_event ev;
                            ev.events = EPOLLOUT | EPOLLONESHOT;
                            ev.data.fd = fd;
                            epoll_ctl(l->epfd, EPOLL_CTL_MOD, fd, &ev);
                            remove_fd = 0;
                        }
                        break;
                    }
                    case FUTURE_KIND_ACCEPT: {
                        int new_fd = accept(fd, NULL, NULL);
                        fut->header.result = new_fd >= 0
                            ? ang_i64((int64_t)new_fd) : ang_i64(-1);
                        fut->header.state = -1;
                        /* Don't close the listener fd */
                        remove_fd = 0;
                        break;
                    }
                    }
                    if (fut->header.state == -1) {
                        /* Cascade waker */
                        if (fut->header.waker_fn) {
                            void (*fn)(void*) = (void(*)(void*))fut->header.waker_fn;
                            fn(fut->header.waker_ctx);
                        }
                    }
                    /* Cleanup fd registration */
                    if (remove_fd) {
                        epoll_ctl(l->epfd, EPOLL_CTL_DEL, fd, NULL);
                    }
                    if (fut->header.state == -1 && remove_fd) {
                        /* Resolved and fd removed: close fd, remove from pending */
                        close(fd);
                        fut->fd = -1;
                        /* Shift array to remove */
                        for (size_t pi2 = 0; pi2 < l->pending_count; pi2++) {
                            if (l->pending[pi2] == fut) {
                                l->pending[pi2] = l->pending[--l->pending_count];
                                break;
                            }
                        }
                    }
                    continue;
                }
            }

            CbEntry* e = cb_registry_find(&l->cbs, fd);
            if (!e) continue;

            /* for timers, read the expiration count */
            int64_t timer_count = 1;
            if (e->is_timer) {
                uint64_t exp = 0;
                read(fd, &exp, sizeof(exp));
                timer_count = (int64_t)exp;
            }

            /* dispatch callback */
            if (ang_is_obj(e->callback)) {
                if (e->is_timer) {
                    AngaraObject targv[1] = { ang_i64(timer_count) };
                    ang_api->call(e->callback, 1, targv);
                } else {
                    ang_api->call(e->callback, 0, NULL);
                }
            }

            /* one-shot timers: remove after firing */
            if (e->is_timer && !e->is_interval) {
                epoll_ctl(l->epfd, EPOLL_CTL_DEL, fd, NULL);
                cb_registry_remove(&l->cbs, fd);
                close(fd);
            }
        }
    }

    return ang_nil();
}

AngaraObject Angara_Loop_stop(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Loop.stop: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (l && l->stop_efd >= 0) {
        uint64_t one = 1;
        write(l->stop_efd, &one, sizeof(one));
    }
    return ang_nil();
}

/* ---- poll (legacy) ---- */

AngaraObject Angara_Loop_poll(int arg_count, AngaraObject* args) {
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_api->list_new();

    int timeout_ms = -1;
    if (arg_count >= 2 && ang_is_i64(args[1]))
        timeout_ms = (int)ang_as_i64(args[1]);

    struct epoll_event events[64];
    int n = epoll_wait(l->epfd, events, 64, timeout_ms);
    if (n <= 0) return ang_api->list_new();

    AngaraObject list = ang_api->list_new();
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        if (fd == l->stop_efd) continue; /* skip internal fd */

        uint32_t revents = events[i].events;

        CbEntry* e = cb_registry_find(&l->cbs, fd);
        int is_timer = e ? e->is_timer : 0;

        AngaraObject rec = ang_api->record_new();
        ang_api->record_set(rec, "fd", ang_i64(fd));

        if (is_timer) {
            ang_api->record_set(rec, "kind", ang_i64(3));
            uint64_t expirations = 0;
            read(fd, &expirations, sizeof(expirations));
            ang_api->record_set(rec, "count", ang_i64((int64_t)expirations));
        } else if (revents & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
            ang_api->record_set(rec, "kind", ang_i64(1));
        } else if (revents & EPOLLOUT) {
            ang_api->record_set(rec, "kind", ang_i64(2));
        }
        ang_api->list_push(list, rec);
        ang_api->decref(rec);
    }
    return list;
}

AngaraObject Angara_Loop_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Loop.close: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (l) {
        l->running = 0;
        cb_registry_free(&l->cbs);
        if (l->stop_efd >= 0) { close(l->stop_efd); l->stop_efd = -1; }
        if (l->epfd >= 0) { close(l->epfd); l->epfd = -1; }
    }
    return ang_nil();
}


/* =========================================================================
   LIB-4: Unified I/O futures — timer, read, write, accept.
   ========================================================================= */

/* Helper: add an IOFuture to the loop's pending array */
static void pending_add(LoopData* l, IOFuture* f) {
    if (l->pending_count >= l->pending_cap) {
        size_t new_cap = l->pending_cap ? l->pending_cap * 2 : 4;
        IOFuture** new_pending = (IOFuture**)realloc(l->pending, new_cap * sizeof(IOFuture*));
        if (!new_pending) return;  /* out of memory — future won't be tracked */
        l->pending = new_pending;
        l->pending_cap = new_cap;
    }
    l->pending[l->pending_count++] = f;
}

/* Helper: allocate and init an IOFuture with the standard header */
static IOFuture* future_alloc(LoopData* l, int kind, size_t extra) {
    IOFuture* f = (IOFuture*)calloc(1, sizeof(IOFuture) + extra);
    f->kind = kind;
    f->header.state = 0;
    f->header.loop = l;
    return f;
}

static void finalize_io_future(void* data) {
    IOFuture* f = (IOFuture*)data;
    if (f->fd >= 0 && f->kind != FUTURE_KIND_ACCEPT) close(f->fd);
    free(f);
}

/* loop.create_timer(ms, value) → Future<T> */
AngaraObject Angara_Loop_create_timer(int arg_count, AngaraObject* args) {
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || arg_count < 3) return ang_nil();

    int64_t ms = ang_is_i64(args[1]) ? ang_as_i64(args[1]) : 0;
    AngaraObject value = args[2];

    IOFuture* f = future_alloc(l, FUTURE_KIND_TIMER, 0);
    f->header.result = value;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        free(f);
        ang_api->throw_error("loop.create_timer: timerfd_create failed.");
        return ang_nil();
    }
    f->fd = tfd;

    struct itimerspec its;
    its.it_value.tv_sec     = ms / 1000;
    its.it_value.tv_nsec    = (ms % 1000) * 1000000L;
    its.it_interval.tv_sec  = 0;
    its.it_interval.tv_nsec = 0;
    timerfd_settime(tfd, 0, &its, NULL);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tfd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, tfd, &ev);

    pending_add(l, f);
    return ang_api->native_instance_new(f, finalize_io_future, "Future");
}

/* loop.read(fd, count) → Future<string> */
AngaraObject Angara_Loop_read(int arg_count, AngaraObject* args) {
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || arg_count < 3) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    int64_t count = ang_as_i64(args[2]);
    if (count <= 0 || count > 1048576) count = 1024;  /* clamp */

    IOFuture* f = future_alloc(l, FUTURE_KIND_READ, (size_t)count);
    f->fd = fd;
    f->count = (size_t)count;

    /* Set non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.fd = fd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev);

    pending_add(l, f);
    return ang_api->native_instance_new(f, finalize_io_future, "Future");
}

/* loop.write(fd, data) → Future<i64> */
AngaraObject Angara_Loop_write(int arg_count, AngaraObject* args) {
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || arg_count < 3) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    const char* ptr = ang_api->as_cstr(args[2]);
    size_t len = ang_api->str_len(args[2]);

    IOFuture* f = future_alloc(l, FUTURE_KIND_WRITE, len);
    f->fd = fd;
    f->count = len;
    f->offset = 0;
    if (len > 0) memcpy(f->buf, ptr, len);

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLONESHOT;
    ev.data.fd = fd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev);

    pending_add(l, f);
    return ang_api->native_instance_new(f, finalize_io_future, "Future");
}

/* loop.accept(fd) → Future<i64> */
AngaraObject Angara_Loop_accept(int arg_count, AngaraObject* args) {
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || arg_count < 2) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);

    IOFuture* f = future_alloc(l, FUTURE_KIND_ACCEPT, 0);
    f->fd = fd;

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.fd = fd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev);

    pending_add(l, f);
    return ang_api->native_instance_new(f, finalize_io_future, "Future");
}

/* loop.run_until(future) → T */
AngaraObject Angara_Loop_run_until(int arg_count, AngaraObject* args) {
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || arg_count < 2) return ang_nil();

    AngaraObject future = args[1];
    if (ang_is_nil(future)) return ang_nil();

    ang_api->future_set_loop(future, l);

    for (;;) {
        if (ang_api->future_state(future) == -1) break;
        AngaraObject run_args[2] = { args[0], ang_i64(10) };
        Angara_Loop_run(2, run_args);
    }

    return ang_api->future_result(future);
}


/* =========================================================================
   Channel — thread-safe bounded MPSC queue with eventfd wakeup
   ========================================================================= */

typedef struct {
    AngaraObject* buf;
    size_t         cap;
    size_t         head;
    size_t         tail;
    size_t         count;
    int            closed;
    int            efd;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    atomic_int     refcount;   // number of live handles (sender + receiver)
} Channel;

static size_t next_pow2(size_t v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4;
    v |= v >> 8; v |= v >> 16;
#if SIZE_MAX > 0xFFFFFFFF
    v |= v >> 32;
#endif
    return v + 1;
}

static Channel* channel_new(size_t cap) {
    Channel* ch = (Channel*)calloc(1, sizeof(Channel));
    if (!ch) return NULL;
    ch->cap = next_pow2(cap);
    if (ch->cap < 4) ch->cap = 4;
    ch->buf = (AngaraObject*)malloc(ch->cap * sizeof(AngaraObject));
    if (!ch->buf) { free(ch); return NULL; }
    ch->efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    atomic_init(&ch->refcount, 2);  // one for sender, one for receiver
    return ch;
}

static void channel_free(Channel* ch) {
    if (!ch) return;
    pthread_mutex_lock(&ch->mutex);
    while (ch->count > 0) {
        ang_api->decref(ch->buf[ch->head]);
        ch->head = (ch->head + 1) & (ch->cap - 1);
        ch->count--;
    }
    pthread_mutex_unlock(&ch->mutex);
    free(ch->buf);
    if (ch->efd >= 0) close(ch->efd);
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch);
}

typedef struct {
    Channel* ch;
    int      is_sender;
} ChannelHandle;

static void finalize_sender(void* data) {
    ChannelHandle* h = (ChannelHandle*)data;
    if (h->ch) {
        pthread_mutex_lock(&h->ch->mutex);
        h->ch->closed = 1;
        pthread_cond_broadcast(&h->ch->not_empty);
        pthread_cond_broadcast(&h->ch->not_full);
        pthread_mutex_unlock(&h->ch->mutex);
        // Last handle to finalize frees the shared Channel
        if (atomic_fetch_sub(&h->ch->refcount, 1) == 1) {
            channel_free(h->ch);
        }
        h->ch = NULL;
    }
    free(h);
}

static void finalize_receiver(void* data) {
    ChannelHandle* h = (ChannelHandle*)data;
    if (h->ch) {
        // Last handle to finalize frees the shared Channel
        if (atomic_fetch_sub(&h->ch->refcount, 1) == 1) {
            channel_free(h->ch);
        }
        h->ch = NULL;
    }
    free(h);
}

AngaraObject Angara_eventloop_channel(int arg_count, AngaraObject* args) {
    size_t cap = 16;
    if (arg_count >= 1 && ang_is_i64(args[0])) {
        int64_t v = ang_as_i64(args[0]);
        if (v > 0) cap = (size_t)v;
    }
    Channel* ch = channel_new(cap);
    if (!ch) { ang_api->throw_error("async.channel: out of memory."); return ang_nil(); }

    ChannelHandle* tx_h = (ChannelHandle*)calloc(1, sizeof(ChannelHandle));
    tx_h->ch = ch; tx_h->is_sender = 1;
    ChannelHandle* rx_h = (ChannelHandle*)calloc(1, sizeof(ChannelHandle));
    rx_h->ch = ch; rx_h->is_sender = 0;

    AngaraObject tx = ang_api->native_instance_new(tx_h, finalize_sender, "Sender");
    AngaraObject rx = ang_api->native_instance_new(rx_h, finalize_receiver, "Receiver");

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "tx", tx);
    ang_api->record_set(rec, "rx", rx);
    ang_api->decref(tx);
    ang_api->decref(rx);
    return rec;
}

AngaraObject Angara_Sender_send(int arg_count, AngaraObject* args) {
    if (arg_count < 2) return ang_nil();
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    Channel* ch = h ? h->ch : NULL;
    if (!ch) return ang_nil();
    pthread_mutex_lock(&ch->mutex);
    while (ch->count >= ch->cap && !ch->closed)
        pthread_cond_wait(&ch->not_full, &ch->mutex);
    if (ch->closed) { pthread_mutex_unlock(&ch->mutex); return ang_nil(); }
    ang_api->incref(args[1]);
    ch->buf[ch->tail] = args[1];
    ch->tail = (ch->tail + 1) & (ch->cap - 1);
    ch->count++;
    uint64_t one = 1;
    write(ch->efd, &one, sizeof(one));
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
    return ang_nil();
}

AngaraObject Angara_Sender_try_send(int arg_count, AngaraObject* args) {
    if (arg_count < 2) return ang_bool(false);
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    Channel* ch = h ? h->ch : NULL;
    if (!ch) return ang_bool(false);
    pthread_mutex_lock(&ch->mutex);
    if (ch->count >= ch->cap || ch->closed) { pthread_mutex_unlock(&ch->mutex); return ang_bool(false); }
    ang_api->incref(args[1]);
    ch->buf[ch->tail] = args[1];
    ch->tail = (ch->tail + 1) & (ch->cap - 1);
    ch->count++;
    uint64_t one = 1;
    write(ch->efd, &one, sizeof(one));
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
    return ang_bool(true);
}

AngaraObject Angara_Sender_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Sender.close: expected 1 argument"); return ang_nil(); }
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    if (h && h->ch) {
        pthread_mutex_lock(&h->ch->mutex);
        h->ch->closed = 1;
        pthread_cond_broadcast(&h->ch->not_empty);
        pthread_cond_broadcast(&h->ch->not_full);
        pthread_mutex_unlock(&h->ch->mutex);
    }
    return ang_nil();
}

AngaraObject Angara_Sender_fileno(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Sender.fileno: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    return ang_i64(h && h->ch ? (int64_t)h->ch->efd : -1);
}

AngaraObject Angara_Receiver_recv(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Receiver.recv: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    Channel* ch = h ? h->ch : NULL;
    if (!ch) return ang_nil();
    pthread_mutex_lock(&ch->mutex);
    while (ch->count == 0 && !ch->closed)
        pthread_cond_wait(&ch->not_empty, &ch->mutex);
    if (ch->count == 0) { pthread_mutex_unlock(&ch->mutex); return ang_nil(); }
    AngaraObject val = ch->buf[ch->head];
    ch->head = (ch->head + 1) & (ch->cap - 1);
    ch->count--;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    return val;
}

AngaraObject Angara_Receiver_try_recv(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Receiver.try_recv: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    Channel* ch = h ? h->ch : NULL;
    if (!ch) return ang_nil();
    pthread_mutex_lock(&ch->mutex);
    if (ch->count == 0) { pthread_mutex_unlock(&ch->mutex); return ang_nil(); }
    AngaraObject val = ch->buf[ch->head];
    ch->head = (ch->head + 1) & (ch->cap - 1);
    ch->count--;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    return val;
}

AngaraObject Angara_Receiver_fileno(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Receiver.fileno: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    return ang_i64(h && h->ch ? (int64_t)h->ch->efd : -1);
}

AngaraObject Angara_Receiver_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("Receiver.close: expected 1 argument"); return ang_nil(); }
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    if (h && h->ch && h->ch->efd >= 0) {
        uint64_t dummy;
        while (read(h->ch->efd, &dummy, sizeof(dummy)) > 0) {}
    }
    return ang_nil();
}


/* =========================================================================
   Export tables
   ========================================================================= */

static const AngaraMethodDef LOOP_METHODS[] = {
    {"on_readable",  (AngaraMethodFn)Angara_Loop_on_readable,  "ia->n"},
    {"on_writable",  (AngaraMethodFn)Angara_Loop_on_writable,  "ia->n"},
    {"set_timeout",  (AngaraMethodFn)Angara_Loop_set_timeout,  "ia->i"},
    {"set_interval", (AngaraMethodFn)Angara_Loop_set_interval, "ia->i"},
    {"clear",        (AngaraMethodFn)Angara_Loop_clear,        "i->n"},
    {"run",          (AngaraMethodFn)Angara_Loop_run,          "i?->n"},
    {"stop",         (AngaraMethodFn)Angara_Loop_stop,         "->n"},
    {"poll",         (AngaraMethodFn)Angara_Loop_poll,         "i?->l<{}>"},
    {"close",        (AngaraMethodFn)Angara_Loop_close,        "->n"},
    /* LIB-4 Stage S-2 / I/O */
    {"create_timer", (AngaraMethodFn)Angara_Loop_create_timer, "ii->f<i>"},
    {"run_until",    (AngaraMethodFn)Angara_Loop_run_until,    "a->a"},
    {"read",         (AngaraMethodFn)Angara_Loop_read,         "ii->f<s>"},
    {"write",        (AngaraMethodFn)Angara_Loop_write,        "is->f<i>"},
    {"accept",       (AngaraMethodFn)Angara_Loop_accept,       "i->f<i>"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef LOOP_CLASS = { "Loop", NULL, LOOP_METHODS };

static const AngaraMethodDef SENDER_METHODS[] = {
    {"send",     (AngaraMethodFn)Angara_Sender_send,     "a->n"},
    {"try_send", (AngaraMethodFn)Angara_Sender_try_send, "a->b"},
    {"close",    (AngaraMethodFn)Angara_Sender_close,    "->n"},
    {"fileno",   (AngaraMethodFn)Angara_Sender_fileno,   "->i"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef SENDER_CLASS = { "Sender", NULL, SENDER_METHODS };

static const AngaraMethodDef RECEIVER_METHODS[] = {
    {"recv",     (AngaraMethodFn)Angara_Receiver_recv,     "->a"},
    {"try_recv", (AngaraMethodFn)Angara_Receiver_try_recv, "->a?"},
    {"close",    (AngaraMethodFn)Angara_Receiver_close,    "->n"},
    {"fileno",   (AngaraMethodFn)Angara_Receiver_fileno,   "->i"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef RECEIVER_CLASS = { "Receiver", NULL, RECEIVER_METHODS };

static const AngaraFuncDef ASYNC_EXPORTS[] = {
    {"Loop",    Angara_eventloop_Loop,    "->Loop",     &LOOP_CLASS},
    {"channel", Angara_eventloop_channel, "i?->{}",     NULL},

    {"READ",     Angara_eventloop_READ,     "->i", NULL},
    {"WRITE",    Angara_eventloop_WRITE,    "->i", NULL},
    {"RDWR",     Angara_eventloop_RDWR,     "->i", NULL},
    {"READABLE", Angara_eventloop_READABLE, "->i", NULL},
    {"WRITABLE", Angara_eventloop_WRITABLE, "->i", NULL},
    {"TIMER",    Angara_eventloop_TIMER,    "->i", NULL},
    {"CHANNEL",  Angara_eventloop_CHANNEL,  "->i", NULL},

    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(eventloop) {
    ang_api = api;
    *def_count = (sizeof(ASYNC_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ASYNC_EXPORTS;
}

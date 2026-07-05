/// Angara async module — epoll event loop + thread-safe channels.
///
/// Poll-driven (no callbacks), integrates with any fd (sockets, timers, channels).
///
///   let loop = async.Loop()
///   loop.add(sock_fd, async.READ)
///   let tid = loop.timer(1000)   // fires every 1000 ms
///   for (ev in loop.poll(500)) { ... }
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
#include <pthread.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

/* =========================================================================
   Event mask constants
   ========================================================================= */

AngaraObject Angara_async_READ(int c, AngaraObject* a)  { (void)c;(void)a; return ang_i64(EPOLLIN); }
AngaraObject Angara_async_WRITE(int c, AngaraObject* a) { (void)c;(void)a; return ang_i64(EPOLLOUT); }
AngaraObject Angara_async_RDWR(int c, AngaraObject* a)  { (void)c;(void)a; return ang_i64(EPOLLIN | EPOLLOUT); }

AngaraObject Angara_async_READABLE(int c, AngaraObject* a) { (void)c;(void)a; return ang_i64(1); }
AngaraObject Angara_async_WRITABLE(int c, AngaraObject* a) { (void)c;(void)a; return ang_i64(2); }
AngaraObject Angara_async_TIMER(int c, AngaraObject* a)    { (void)c;(void)a; return ang_i64(3); }
AngaraObject Angara_async_CHANNEL(int c, AngaraObject* a)   { (void)c;(void)a; return ang_i64(4); }


/* =========================================================================
   Loop — epoll wrapper
   ========================================================================= */

typedef struct {
    int epfd;
    int* timer_fds;     /* track timer fds for cleanup */
    size_t timer_count;
    size_t timer_cap;
} LoopData;

static void finalize_loop(void* data) {
    LoopData* l = (LoopData*)data;
    for (size_t i = 0; i < l->timer_count; i++) {
        if (l->timer_fds[i] >= 0) close(l->timer_fds[i]);
    }
    free(l->timer_fds);
    if (l->epfd >= 0) close(l->epfd);
    free(l);
}

AngaraObject Angara_async_Loop(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        ang_api->throw_error("async.Loop: epoll_create1 failed.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)calloc(1, sizeof(LoopData));
    l->epfd = epfd;
    l->timer_cap = 4;
    l->timer_fds = (int*)malloc(l->timer_cap * sizeof(int));
    return ang_api->native_instance_new(l, finalize_loop, "Loop");
}

AngaraObject Angara_Loop_add(int arg_count, AngaraObject* args) {
    /* loop.add(fd, mask) */
    if (arg_count < 3 || !ang_is_i64(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("loop.add(fd, mask) expects two i64 values.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    uint32_t mask = (uint32_t)ang_as_i64(args[2]);

    struct epoll_event ev;
    ev.events = mask | EPOLLET;   /* edge-triggered for efficiency */
    ev.data.fd = fd;
    if (epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "loop.add: epoll_ctl ADD fd=%d: %s", fd, strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_Loop_mod(int arg_count, AngaraObject* args) {
    if (arg_count < 3 || !ang_is_i64(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("loop.mod(fd, mask) expects two i64 values.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    uint32_t mask = (uint32_t)ang_as_i64(args[2]);

    struct epoll_event ev;
    ev.events = mask | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(l->epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "loop.mod: epoll_ctl MOD fd=%d: %s", fd, strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_Loop_remove(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_i64(args[1])) {
        ang_api->throw_error("loop.remove(fd) expects an i64.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_nil();

    int fd = (int)ang_as_i64(args[1]);
    epoll_ctl(l->epfd, EPOLL_CTL_DEL, fd, NULL);
    return ang_nil();
}

AngaraObject Angara_Loop_timer(int arg_count, AngaraObject* args) {
    /* loop.timer(interval_ms) → timer_fd (i64) */
    if (arg_count < 2 || !ang_is_i64(args[1])) {
        ang_api->throw_error("loop.timer(interval_ms) expects an i64.");
        return ang_nil();
    }
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (!l || l->epfd < 0) return ang_i64(-1);

    int64_t interval_ms = ang_as_i64(args[1]);
    if (interval_ms < 1) interval_ms = 1;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        ang_api->throw_error("loop.timer: timerfd_create failed.");
        return ang_i64(-1);
    }

    struct itimerspec its;
    its.it_value.tv_sec     = interval_ms / 1000;
    its.it_value.tv_nsec    = (interval_ms % 1000) * 1000000L;
    its.it_interval.tv_sec  = interval_ms / 1000;
    its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000L;
    timerfd_settime(tfd, 0, &its, NULL);

    /* register the timer fd for reading */
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = tfd;
    epoll_ctl(l->epfd, EPOLL_CTL_ADD, tfd, &ev);

    /* track for cleanup */
    if (l->timer_count >= l->timer_cap) {
        l->timer_cap *= 2;
        l->timer_fds = (int*)realloc(l->timer_fds, l->timer_cap * sizeof(int));
    }
    l->timer_fds[l->timer_count++] = tfd;

    return ang_i64(tfd);
}

AngaraObject Angara_Loop_poll(int arg_count, AngaraObject* args) {
    /* loop.poll(timeout_ms?) → list<{fd, kind, is_timer}> */
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
        uint32_t revents = events[i].events;

        /* check if this fd is one of our timer fds */
        int is_timer = 0;
        for (size_t j = 0; j < l->timer_count; j++) {
            if (l->timer_fds[j] == fd) { is_timer = 1; break; }
        }

        AngaraObject rec = ang_api->record_new();
        ang_api->record_set(rec, "fd", ang_i64(fd));

        if (is_timer) {
            ang_api->record_set(rec, "kind", ang_i64(3));  /* TIMER */
            /* read the timerfd to clear the event and get expiration count */
            uint64_t expirations = 0;
            read(fd, &expirations, sizeof(expirations));
            ang_api->record_set(rec, "count", ang_i64((int64_t)expirations));
        } else if (revents & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
            ang_api->record_set(rec, "kind", ang_i64(1));  /* READABLE */
        } else if (revents & EPOLLOUT) {
            ang_api->record_set(rec, "kind", ang_i64(2));  /* WRITABLE */
        }

        ang_api->list_push(list, rec);
        ang_api->decref(rec);
    }
    return list;
}

AngaraObject Angara_Loop_close(int arg_count, AngaraObject* args) {
    LoopData* l = (LoopData*)ang_api->native_instance_data(args[0]);
    if (l) {
        for (size_t i = 0; i < l->timer_count; i++) {
            if (l->timer_fds[i] >= 0) close(l->timer_fds[i]);
        }
        free(l->timer_fds);
        l->timer_fds = NULL;
        l->timer_count = 0;
        l->timer_cap = 0;
        if (l->epfd >= 0) { close(l->epfd); l->epfd = -1; }
    }
    return ang_nil();
}


/* =========================================================================
   Channel — thread-safe bounded MPSC queue with eventfd wakeup
   ========================================================================= */

typedef struct {
    AngaraObject* buf;       /* ring buffer */
    size_t         cap;       /* capacity (must be power of two, enforced) */
    size_t         head;      /* read index */
    size_t         tail;      /* write index */
    size_t         count;     /* current item count */
    int            closed;    /* set when closed */
    int            efd;       /* eventfd for epoll wakeup on recv side */
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} Channel;

/* round up to next power of two */
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
    return ch;
}

static void channel_free(Channel* ch) {
    if (!ch) return;
    /* decref any remaining items */
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
    int      is_sender;   /* 1 = sender, 0 = receiver */
} ChannelHandle;

static void finalize_sender(void* data) {
    ChannelHandle* h = (ChannelHandle*)data;
    if (h->ch) {
        pthread_mutex_lock(&h->ch->mutex);
        h->ch->closed = 1;
        pthread_cond_broadcast(&h->ch->not_empty);
        pthread_cond_broadcast(&h->ch->not_full);
        pthread_mutex_unlock(&h->ch->mutex);
        channel_free(h->ch);
        h->ch = NULL;
    }
    free(h);
}

static void finalize_receiver(void* data) {
    /* receiver doesn't own the channel — sender does.
       just close our eventfd reference and free the handle */
    ChannelHandle* h = (ChannelHandle*)data;
    free(h);
}

AngaraObject Angara_async_channel(int arg_count, AngaraObject* args) {
    /* async.channel(capacity?) → {tx: Sender, rx: Receiver} */
    size_t cap = 16;
    if (arg_count >= 1 && ang_is_i64(args[0])) {
        int64_t v = ang_as_i64(args[0]);
        if (v > 0) cap = (size_t)v;
    }

    Channel* ch = channel_new(cap);
    if (!ch) {
        ang_api->throw_error("async.channel: out of memory.");
        return ang_nil();
    }

    ChannelHandle* tx_h = (ChannelHandle*)calloc(1, sizeof(ChannelHandle));
    tx_h->ch = ch;
    tx_h->is_sender = 1;

    ChannelHandle* rx_h = (ChannelHandle*)calloc(1, sizeof(ChannelHandle));
    rx_h->ch = ch;
    rx_h->is_sender = 0;

    AngaraObject tx = ang_api->native_instance_new(tx_h, finalize_sender, "Sender");
    AngaraObject rx = ang_api->native_instance_new(rx_h, finalize_receiver, "Receiver");

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "tx", tx);
    ang_api->record_set(rec, "rx", rx);
    ang_api->decref(tx);
    ang_api->decref(rx);
    return rec;
}


/* ---- Sender ---- */

AngaraObject Angara_Sender_send(int arg_count, AngaraObject* args) {
    /* sender.send(value) — blocking */
    if (arg_count < 2) return ang_nil();
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    Channel* ch = h ? h->ch : NULL;
    if (!ch) return ang_nil();

    pthread_mutex_lock(&ch->mutex);
    while (ch->count >= ch->cap && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->mutex);
    }
    if (ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return ang_nil();
    }
    ang_api->incref(args[1]);
    ch->buf[ch->tail] = args[1];
    ch->tail = (ch->tail + 1) & (ch->cap - 1);
    ch->count++;
    /* wake up receiver via eventfd */
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
    if (ch->count >= ch->cap || ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return ang_bool(false);
    }
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
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    return ang_i64(h && h->ch ? (int64_t)h->ch->efd : -1);
}


/* ---- Receiver ---- */

AngaraObject Angara_Receiver_recv(int arg_count, AngaraObject* args) {
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    Channel* ch = h ? h->ch : NULL;
    if (!ch) return ang_nil();

    pthread_mutex_lock(&ch->mutex);
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->mutex);
    }
    if (ch->count == 0) {
        pthread_mutex_unlock(&ch->mutex);
        return ang_nil();  /* closed + empty */
    }
    AngaraObject val = ch->buf[ch->head];
    ch->head = (ch->head + 1) & (ch->cap - 1);
    ch->count--;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    return val;  /* caller takes ownership (no decref needed — we transferred) */
}

AngaraObject Angara_Receiver_try_recv(int arg_count, AngaraObject* args) {
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    Channel* ch = h ? h->ch : NULL;
    if (!ch) return ang_nil();

    pthread_mutex_lock(&ch->mutex);
    if (ch->count == 0) {
        pthread_mutex_unlock(&ch->mutex);
        return ang_nil();
    }
    AngaraObject val = ch->buf[ch->head];
    ch->head = (ch->head + 1) & (ch->cap - 1);
    ch->count--;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    return val;
}

AngaraObject Angara_Receiver_fileno(int arg_count, AngaraObject* args) {
    (void)arg_count;
    ChannelHandle* h = (ChannelHandle*)ang_api->native_instance_data(args[0]);
    return ang_i64(h && h->ch ? (int64_t)h->ch->efd : -1);
}

AngaraObject Angara_Receiver_close(int arg_count, AngaraObject* args) {
    /* receiver close is a no-op (sender owns the channel).
       just drain any pending eventfd writes so epoll doesn't spin */
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
    {"add",    (AngaraMethodFn)Angara_Loop_add,    "ii->n"},
    {"mod",    (AngaraMethodFn)Angara_Loop_mod,    "ii->n"},
    {"remove", (AngaraMethodFn)Angara_Loop_remove, "i->n"},
    {"timer",  (AngaraMethodFn)Angara_Loop_timer,  "i->i"},
    {"poll",   (AngaraMethodFn)Angara_Loop_poll,   "i?->l<{}>"},
    {"close",  (AngaraMethodFn)Angara_Loop_close,  "->n"},
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
    {"Loop",    Angara_async_Loop,    "->Loop",     &LOOP_CLASS},
    {"channel", Angara_async_channel, "i?->{}",     NULL},  /* returns {tx, rx} */

    /* event mask constants */
    {"READ",     Angara_async_READ,     "->i", NULL},
    {"WRITE",    Angara_async_WRITE,    "->i", NULL},
    {"RDWR",     Angara_async_RDWR,     "->i", NULL},
    /* poll result kind constants */
    {"READABLE", Angara_async_READABLE, "->i", NULL},
    {"WRITABLE", Angara_async_WRITABLE, "->i", NULL},
    {"TIMER",    Angara_async_TIMER,    "->i", NULL},
    {"CHANNEL",  Angara_async_CHANNEL,  "->i", NULL},

    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(async) {
    ang_api = api;
    *def_count = (sizeof(ASYNC_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ASYNC_EXPORTS;
}

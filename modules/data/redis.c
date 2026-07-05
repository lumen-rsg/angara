/// Angara Redis module — hiredis-based client.
///
///   let r = redis.connect("127.0.0.1", 6379)
///   r.cmd("SET", "key", "value")
///   let val = r.cmd("GET", "key")
///   r.close()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

/* convert a redisReply to an AngaraObject (recursive for arrays) */
static AngaraObject reply_to_angara(redisReply* r) {
    if (!r) return ang_nil();

    switch (r->type) {
        case REDIS_REPLY_STRING:
            return ang_api->string_len(r->str, r->len);
        case REDIS_REPLY_INTEGER:
            return ang_i64(r->integer);
        case REDIS_REPLY_NIL:
            return ang_nil();
        case REDIS_REPLY_STATUS:
            return ang_api->string_len(r->str, r->len);
        case REDIS_REPLY_ERROR: {
            /* throw the error as an Angara exception */
            ang_api->throw_error(r->str);
            return ang_nil();
        }
        case REDIS_REPLY_ARRAY: {
            AngaraObject list = ang_api->list_new();
            for (size_t i = 0; i < r->elements; i++) {
                AngaraObject elem = reply_to_angara(r->element[i]);
                ang_api->list_push(list, elem);
                ang_api->decref(elem);
            }
            return list;
        }
        default:
            return ang_nil();
    }
}

typedef struct {
    redisContext* ctx;
} RedisConn;

static void finalize_redis(void* data) {
    RedisConn* r = (RedisConn*)data;
    if (r->ctx) redisFree(r->ctx);
    free(r);
}

AngaraObject Angara_redis_connect(int arg_count, AngaraObject* args) {
    const char* host = "127.0.0.1";
    int port = 6379;

    if (arg_count >= 1 && IS_STR(args[0])) host = ang_api->as_cstr(args[0]);
    if (arg_count >= 2 && ang_is_i64(args[1])) port = (int)ang_as_i64(args[1]);

    redisContext* ctx = redisConnect(host, port);
    if (!ctx || ctx->err) {
        char buf[256];
        snprintf(buf, sizeof(buf), "redis.connect: %s",
                 ctx ? ctx->errstr : "out of memory");
        if (ctx) redisFree(ctx);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    RedisConn* rc = (RedisConn*)calloc(1, sizeof(RedisConn));
    rc->ctx = ctx;
    return ang_api->native_instance_new(rc, finalize_redis, "RedisConn");
}

AngaraObject Angara_RedisConn_cmd(int arg_count, AngaraObject* args) {
    /* conn.cmd("SET", "key", "val") — variadic, all args become redisCommand args */
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("redis.cmd(command, ...) expects at least a command string.");
        return ang_nil();
    }
    RedisConn* rc = (RedisConn*)ang_api->native_instance_data(args[0]);
    if (!rc || !rc->ctx) {
        ang_api->throw_error("redis.cmd: connection is closed.");
        return ang_nil();
    }

    /* build a format string and argv for redisCommandArgv */
    int n_args = arg_count - 1;
    const char** argv = (const char**)malloc((size_t)n_args * sizeof(const char*));
    size_t* argv_len = (size_t*)malloc((size_t)n_args * sizeof(size_t));

    for (int i = 0; i < n_args; i++) {
        AngaraObject a = args[i + 1];
        if (IS_STR(a)) {
            argv[i] = ang_api->as_cstr(a);
            argv_len[i] = ang_api->str_len(a);
        } else {
            /* convert non-string args via to_string */
            AngaraObject s = ang_api->to_string(a);
            /* we need a stable pointer — copy to heap */
            const char* cs = ang_api->as_cstr(s);
            size_t slen = ang_api->str_len(s);
            char* copy = (char*)malloc(slen + 1);
            memcpy(copy, cs, slen);
            copy[slen] = '\0';
            argv[i] = copy;
            argv_len[i] = slen;
            ang_api->decref(s);
        }
    }

    redisReply* reply = redisCommandArgv(rc->ctx, n_args, argv, argv_len);

    /* free any heap-allocated copies for non-string args */
    for (int i = 0; i < n_args; i++) {
        if (!IS_STR(args[i + 1])) free((void*)argv[i]);
    }
    free(argv);
    free(argv_len);

    if (!reply) {
        char buf[256];
        snprintf(buf, sizeof(buf), "redis.cmd: %s", rc->ctx->errstr);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    AngaraObject result = reply_to_angara(reply);
    freeReplyObject(reply);
    return result;
}

AngaraObject Angara_RedisConn_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    RedisConn* rc = (RedisConn*)ang_api->native_instance_data(args[0]);
    if (rc && rc->ctx) {
        redisFree(rc->ctx);
        rc->ctx = NULL;
    }
    return ang_nil();
}


static const AngaraMethodDef REDIS_METHODS[] = {
    {"cmd",   (AngaraMethodFn)Angara_RedisConn_cmd,   "sa*->a"},
    {"close", (AngaraMethodFn)Angara_RedisConn_close, "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef REDIS_CLASS = { "RedisConn", NULL, REDIS_METHODS };

static const AngaraFuncDef REDIS_EXPORTS[] = {
    {"connect", Angara_redis_connect, "si?->RedisConn", &REDIS_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(redis) {
    ang_api = api;
    *def_count = (sizeof(REDIS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return REDIS_EXPORTS;
}

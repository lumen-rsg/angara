/// Angara SQLite module — database open, query, execute, pool, transactions.
/// Depends: sqlite3.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "Angara.h"
#include <sqlite3.h>

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

typedef struct {
    sqlite3* db;
} DbConn;

static void finalize_db(void* data) {
    DbConn* dbc = (DbConn*)data;
    if (dbc->db) sqlite3_close(dbc->db);
    free(dbc);
}

AngaraObject Angara_sqlite_open(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("sqlite.open: expected 1 argument"); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);

    DbConn* dbc = (DbConn*)calloc(1, sizeof(DbConn));
    int rc = sqlite3_open(path, &dbc->db);
    if (rc != SQLITE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "sqlite.open: %s", sqlite3_errmsg(dbc->db));
        sqlite3_close(dbc->db);
        free(dbc);
        ang_api->throw_error(buf);
        return ang_nil();
    }
    sqlite3_exec(dbc->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    return ang_api->native_instance_new(dbc, finalize_db, "SqliteDb");
}

AngaraObject Angara_SqliteDb_execute(int arg_count, AngaraObject* args) {
    if (arg_count < 3) { ang_api->throw_error("SqliteDb.execute: expected 3 arguments"); return ang_nil(); }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) { ang_api->throw_error("execute: database is closed."); return ang_nil(); }

    const char* sql = ang_api->as_cstr(args[1]);

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(dbc->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "sqlite execute: %s", sqlite3_errmsg(dbc->db));
        sqlite3_finalize(stmt);  /* M19: free prepared statement before throw */
        ang_api->throw_error(buf);
        return ang_nil();
    }

    if (arg_count >= 3 && IS_LIST(args[2])) {
        size_t plen = ang_api->list_len(args[2]);
        for (size_t i = 0; i < plen; i++) {
            AngaraObject p = ang_api->list_get(args[2], (int64_t)i);
            int idx = (int)i + 1;
            if (ang_is_nil(p)) {
                sqlite3_bind_null(stmt, idx);
            } else if (ang_is_i64(p)) {
                sqlite3_bind_int64(stmt, idx, ang_as_i64(p));
            } else if (ang_is_f64(p)) {
                sqlite3_bind_double(stmt, idx, ang_as_f64(p));
            } else if (ang_is_bool(p)) {
                sqlite3_bind_int(stmt, idx, ang_as_bool(p) ? 1 : 0);
            } else if (IS_STR(p)) {
                const char* s = ang_api->as_cstr(p);
                size_t slen = ang_api->str_len(p);
                sqlite3_bind_text(stmt, idx, s, (int)slen, SQLITE_TRANSIENT);
            }
            ang_api->decref(p);
        }
    }

    AngaraObject rows = ang_api->list_new();

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AngaraObject row = ang_api->record_new();
        int cols = sqlite3_column_count(stmt);
        for (int c = 0; c < cols; c++) {
            const char* col_name = sqlite3_column_name(stmt, c);
            if (!col_name) col_name = "";

            int col_type = sqlite3_column_type(stmt, c);
            AngaraObject val;
            switch (col_type) {
                case SQLITE_INTEGER:
                    val = ang_i64(sqlite3_column_int64(stmt, c));
                    break;
                case SQLITE_FLOAT:
                    val = ang_f64(sqlite3_column_double(stmt, c));
                    break;
                case SQLITE_TEXT: {
                    const char* text = (const char*)sqlite3_column_text(stmt, c);
                    int text_len = sqlite3_column_bytes(stmt, c);
                    val = ang_api->string_len(text, (size_t)text_len);
                    break;
                }
                case SQLITE_BLOB: {
                    const char* blob = (const char*)sqlite3_column_blob(stmt, c);
                    int blob_len = sqlite3_column_bytes(stmt, c);
                    val = ang_api->string_len(blob, (size_t)blob_len);
                    break;
                }
                default:
                    val = ang_nil();
                    break;
            }
            ang_api->record_set(row, col_name, val);
            ang_api->decref(val);
        }
        ang_api->list_push(rows, row);
        ang_api->decref(row);
    }

    sqlite3_finalize(stmt);
    return rows;
}

AngaraObject Angara_SqliteDb_query_one(int arg_count, AngaraObject* args) {
    if (arg_count < 3) { ang_api->throw_error("SqliteDb.query_one: expected 3 arguments"); return ang_nil(); }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) return ang_nil();

    const char* sql = ang_api->as_cstr(args[1]);
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(dbc->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return ang_nil();

    if (arg_count >= 3 && IS_LIST(args[2])) {
        size_t plen = ang_api->list_len(args[2]);
        for (size_t i = 0; i < plen; i++) {
            AngaraObject p = ang_api->list_get(args[2], (int64_t)i);
            int idx = (int)i + 1;
            if (ang_is_i64(p)) sqlite3_bind_int64(stmt, idx, ang_as_i64(p));
            else if (ang_is_f64(p)) sqlite3_bind_double(stmt, idx, ang_as_f64(p));
            else if (IS_STR(p)) sqlite3_bind_text(stmt, idx, ang_api->as_cstr(p), -1, SQLITE_TRANSIENT);
            else if (ang_is_nil(p)) sqlite3_bind_null(stmt, idx);
            ang_api->decref(p);
        }
    }

    AngaraObject result = ang_nil();
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = ang_api->record_new();
        int cols = sqlite3_column_count(stmt);
        for (int c = 0; c < cols; c++) {
            const char* col_name = sqlite3_column_name(stmt, c);
            int col_type = sqlite3_column_type(stmt, c);
            AngaraObject val;
            switch (col_type) {
                case SQLITE_INTEGER: val = ang_i64(sqlite3_column_int64(stmt, c)); break;
                case SQLITE_FLOAT:   val = ang_f64(sqlite3_column_double(stmt, c)); break;
                case SQLITE_TEXT:    val = ang_api->string((const char*)sqlite3_column_text(stmt, c)); break;
                default:             val = ang_nil(); break;
            }
            ang_api->record_set(result, col_name ? col_name : "", val);
            ang_api->decref(val);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

AngaraObject Angara_SqliteDb_run(int arg_count, AngaraObject* args) {
    if (arg_count < 3) { ang_api->throw_error("SqliteDb.run: expected 3 arguments"); return ang_nil(); }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) { ang_api->throw_error("run: database is closed."); return ang_nil(); }

    const char* sql = ang_api->as_cstr(args[1]);
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(dbc->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "sqlite run: %s", sqlite3_errmsg(dbc->db));
        sqlite3_finalize(stmt);  /* M19: free prepared statement before throw */
        ang_api->throw_error(buf);
        return ang_nil();
    }

    if (arg_count >= 3 && IS_LIST(args[2])) {
        size_t plen = ang_api->list_len(args[2]);
        for (size_t i = 0; i < plen; i++) {
            AngaraObject p = ang_api->list_get(args[2], (int64_t)i);
            int idx = (int)i + 1;
            if (ang_is_i64(p)) sqlite3_bind_int64(stmt, idx, ang_as_i64(p));
            else if (ang_is_f64(p)) sqlite3_bind_double(stmt, idx, ang_as_f64(p));
            else if (IS_STR(p)) sqlite3_bind_text(stmt, idx, ang_api->as_cstr(p), -1, SQLITE_TRANSIENT);
            else if (ang_is_nil(p)) sqlite3_bind_null(stmt, idx);
            ang_api->decref(p);
        }
    }

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return ang_i64((int64_t)sqlite3_last_insert_rowid(dbc->db));
}

AngaraObject Angara_SqliteDb_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqliteDb.close: expected 1 argument"); return ang_nil(); }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (dbc && dbc->db) {
        sqlite3_close(dbc->db);
        dbc->db = NULL;
    }
    return ang_nil();
}

AngaraObject Angara_SqliteDb_last_insert_id(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqliteDb.last_insert_id: expected 1 argument"); return ang_nil(); }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) return ang_i64(-1);
    return ang_i64((int64_t)sqlite3_last_insert_rowid(dbc->db));
}

AngaraObject Angara_SqliteDb_changes(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqliteDb.changes: expected 1 argument"); return ang_nil(); }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) return ang_i64(0);
    return ang_i64((int64_t)sqlite3_changes(dbc->db));
}

/* ---- transaction API ---- */

AngaraObject Angara_SqliteDb_begin(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqliteDb.begin: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) { ang_api->throw_error("begin: database is closed."); return ang_nil(); }
    char* err = NULL;
    if (sqlite3_exec(dbc->db, "BEGIN", NULL, NULL, &err) != SQLITE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "sqlite begin: %s", err ? err : "unknown");
        if (err) sqlite3_free(err);
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_SqliteDb_commit(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqliteDb.commit: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) { ang_api->throw_error("commit: database is closed."); return ang_nil(); }
    char* err = NULL;
    if (sqlite3_exec(dbc->db, "COMMIT", NULL, NULL, &err) != SQLITE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "sqlite commit: %s", err ? err : "unknown");
        if (err) sqlite3_free(err);
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_SqliteDb_rollback(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqliteDb.rollback: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) { ang_api->throw_error("rollback: database is closed."); return ang_nil(); }
    char* err = NULL;
    if (sqlite3_exec(dbc->db, "ROLLBACK", NULL, NULL, &err) != SQLITE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "sqlite rollback: %s", err ? err : "unknown");
        if (err) sqlite3_free(err);
        ang_api->throw_error(buf);
    }
    return ang_nil();
}


/* ---- connection pool ---- */

typedef struct {
    DbConn**  conns;
    size_t    count;
    size_t    cap;
    char*     path;
    pthread_mutex_t mutex;
} PoolData;

static void finalize_pool(void* data) {
    PoolData* p = (PoolData*)data;
    for (size_t i = 0; i < p->count; i++) {
        if (p->conns[i]->db) sqlite3_close(p->conns[i]->db);
        free(p->conns[i]);
    }
    free(p->conns);
    free(p->path);
    pthread_mutex_destroy(&p->mutex);
    free(p);
}

AngaraObject Angara_sqlite_pool(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("sqlite.pool: expected 2 arguments"); return ang_nil(); }
    const char* path = ang_api->as_cstr(args[0]);
    int64_t size = ang_as_i64(args[1]);
    if (size < 1) size = 1;
    if (size > 64) size = 64;

    PoolData* p = (PoolData*)calloc(1, sizeof(PoolData));
    p->path = strdup(path);
    p->cap = (size_t)size;
    p->conns = (DbConn**)calloc(p->cap, sizeof(DbConn*));
    pthread_mutex_init(&p->mutex, NULL);

    /* pre-open all connections */
    for (size_t i = 0; i < p->cap; i++) {
        DbConn* dbc = (DbConn*)calloc(1, sizeof(DbConn));
        int rc = sqlite3_open(path, &dbc->db);
        if (rc == SQLITE_OK) {
            sqlite3_exec(dbc->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
            p->conns[p->count++] = dbc;
        } else {
            sqlite3_close(dbc->db);
            free(dbc);
        }
    }

    if (p->count == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "sqlite.pool: failed to open any connections to '%s'", path);
        finalize_pool(p);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    return ang_api->native_instance_new(p, finalize_pool, "SqlitePool");
}

AngaraObject Angara_SqlitePool_acquire(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqlitePool.acquire: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    PoolData* p = (PoolData*)ang_api->native_instance_data(args[0]);
    if (!p) return ang_nil();

    pthread_mutex_lock(&p->mutex);
    while (p->count == 0) {
        pthread_mutex_unlock(&p->mutex);
        usleep(1000);  /* 1 ms */
        pthread_mutex_lock(&p->mutex);
    }
    DbConn* dbc = p->conns[--p->count];
    pthread_mutex_unlock(&p->mutex);

    /* return conn as native instance; caller must pool.release(conn) when done.
       no finalizer — the pool owns the sqlite3 handle. */
    return ang_api->native_instance_new(dbc, NULL, "SqliteDb");
}

/* for release we need a different approach — just re-store the conn in the pool */
AngaraObject Angara_SqlitePool_release(int arg_count, AngaraObject* args) {
    if (arg_count < 2) return ang_nil();
    PoolData* p = (PoolData*)ang_api->native_instance_data(args[0]);
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[1]);
    if (!p || !dbc || !dbc->db) return ang_nil();

    pthread_mutex_lock(&p->mutex);
    if (p->count < p->cap) {
        p->conns[p->count++] = dbc;
    } else {
        /* pool is full — close this extra connection */
        sqlite3_close(dbc->db);
        free(dbc);
    }
    pthread_mutex_unlock(&p->mutex);
    return ang_nil();
}

AngaraObject Angara_SqlitePool_close(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("SqlitePool.close: expected 1 argument"); return ang_nil(); }
    (void)arg_count;
    PoolData* p = (PoolData*)ang_api->native_instance_data(args[0]);
    if (p) {
        for (size_t i = 0; i < p->count; i++) {
            if (p->conns[i]->db) sqlite3_close(p->conns[i]->db);
            free(p->conns[i]);
        }
        p->count = 0;
    }
    return ang_nil();
}

static const AngaraMethodDef DB_METHODS[] = {
    {"execute",        (AngaraMethodFn)Angara_SqliteDb_execute,        "sl<a>?->l<{}>"},
    {"query_one",      (AngaraMethodFn)Angara_SqliteDb_query_one,      "sl<a>?->{}?"},
    {"run",            (AngaraMethodFn)Angara_SqliteDb_run,             "sl<a>?->i"},
    {"close",          (AngaraMethodFn)Angara_SqliteDb_close,           "->n"},
    {"last_insert_id", (AngaraMethodFn)Angara_SqliteDb_last_insert_id,  "->i"},
    {"changes",        (AngaraMethodFn)Angara_SqliteDb_changes,         "->i"},
    {"begin",          (AngaraMethodFn)Angara_SqliteDb_begin,           "->n"},
    {"commit",         (AngaraMethodFn)Angara_SqliteDb_commit,          "->n"},
    {"rollback",       (AngaraMethodFn)Angara_SqliteDb_rollback,        "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef DB_CLASS = { "SqliteDb", NULL, DB_METHODS };

static const AngaraMethodDef POOL_METHODS[] = {
    {"acquire", (AngaraMethodFn)Angara_SqlitePool_acquire, "->SqliteDb?"},
    {"release", (AngaraMethodFn)Angara_SqlitePool_release, "SqliteDb->n"},
    {"close",   (AngaraMethodFn)Angara_SqlitePool_close,   "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef POOL_CLASS = { "SqlitePool", NULL, POOL_METHODS };

static const AngaraFuncDef SQLITE_EXPORTS[] = {
    {"open", Angara_sqlite_open, "s->SqliteDb", &DB_CLASS},
    {"pool", Angara_sqlite_pool, "si->SqlitePool", &POOL_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(sqlite) {
    ang_api = api;
    *def_count = (sizeof(SQLITE_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return SQLITE_EXPORTS;
}
//
// sqlite.c — Angara SQLite database module
//
// Provides SQLite database access: open, query, execute, prepared statements.
// Depends on SQLite3.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Angara.h"
#include <sqlite3.h>

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

// ---------------------------------------------------------------------------
// Database connection (native instance)
// ---------------------------------------------------------------------------

typedef struct {
    sqlite3* db;
} DbConn;

static void finalize_db(void* data) {
    DbConn* dbc = (DbConn*)data;
    if (dbc->db) sqlite3_close(dbc->db);
    free(dbc);
}

// --- sqlite.open(path) -> Db ---
AngaraObject Angara_sqlite_open(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("open(path) expects a string.");
        return ang_nil();
    }
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
    // Enable WAL mode for better concurrency
    sqlite3_exec(dbc->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    return ang_api->native_instance_new(dbc, finalize_db, "SqliteDb");
}

// --- Db.execute(sql, params?) -> list<record> ---
AngaraObject Angara_Db_execute(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("execute(sql, params?) expects a string.");
        return ang_nil();
    }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) { ang_api->throw_error("execute: database is closed."); return ang_nil(); }

    const char* sql = ang_api->as_cstr(args[1]);

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(dbc->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "sqlite execute: %s", sqlite3_errmsg(dbc->db));
        ang_api->throw_error(buf);
        return ang_nil();
    }

    // Bind parameters (list of values)
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

// --- Db.query_one(sql, params?) -> record? ---
AngaraObject Angara_Db_query_one(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("query_one(sql, params?) expects a string.");
        return ang_nil();
    }
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

// --- Db.run(sql, params?) -> i64  (returns last_insert_rowid) ---
AngaraObject Angara_Db_run(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("run(sql, params?) expects a string.");
        return ang_nil();
    }
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) { ang_api->throw_error("run: database is closed."); return ang_nil(); }

    const char* sql = ang_api->as_cstr(args[1]);
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(dbc->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "sqlite run: %s", sqlite3_errmsg(dbc->db));
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

// --- Db.close() -> nil ---
AngaraObject Angara_Db_close(int arg_count, AngaraObject* args) {
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (dbc && dbc->db) {
        sqlite3_close(dbc->db);
        dbc->db = NULL;
    }
    return ang_nil();
}

// --- Db.last_insert_id() -> i64 ---
AngaraObject Angara_Db_last_insert_id(int arg_count, AngaraObject* args) {
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) return ang_i64(-1);
    return ang_i64((int64_t)sqlite3_last_insert_rowid(dbc->db));
}

// --- Db.changes() -> i64 ---
AngaraObject Angara_Db_changes(int arg_count, AngaraObject* args) {
    DbConn* dbc = (DbConn*)ang_api->native_instance_data(args[0]);
    if (!dbc || !dbc->db) return ang_i64(0);
    return ang_i64((int64_t)sqlite3_changes(dbc->db));
}

// --- Export Tables ---

static const AngaraMethodDef DB_METHODS[] = {
    {"execute",        (AngaraMethodFn)Angara_Db_execute,        "sl?->l<{}>"},
    {"query_one",      (AngaraMethodFn)Angara_Db_query_one,      "sl?->{}?"},
    {"run",            (AngaraMethodFn)Angara_Db_run,             "sl?->i"},
    {"close",          (AngaraMethodFn)Angara_Db_close,           "->n"},
    {"last_insert_id", (AngaraMethodFn)Angara_Db_last_insert_id,  "->i"},
    {"changes",        (AngaraMethodFn)Angara_Db_changes,         "->i"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef DB_CLASS = { "SqliteDb", NULL, DB_METHODS };

static const AngaraFuncDef SQLITE_EXPORTS[] = {
    {"open", Angara_sqlite_open, "s->SqliteDb", &DB_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(sqlite) {
    ang_api = api;
    *def_count = (sizeof(SQLITE_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return SQLITE_EXPORTS;
}
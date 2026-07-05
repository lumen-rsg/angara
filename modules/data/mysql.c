/// Angara MySQL / MariaDB module — libmysqlclient / libmariadb client.
///
///   let db = mysql.connect("localhost", "user", "pass", "mydb")
///   let rows = db.query("SELECT id, name FROM users WHERE id = ?", [42])
///   db.execute("INSERT INTO logs (msg) VALUES (?)", ["hello"])
///   db.close()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
/* mariadb's ma_list.h #defines list_push — clobbers ang_api->list_push */
#undef list_push
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

typedef struct {
    MYSQL* conn;
} MyConn;

static void finalize_my(void* data) {
    MyConn* m = (MyConn*)data;
    if (m->conn) { mysql_close(m->conn); free(m->conn); }
    free(m);
}

AngaraObject Angara_mysql_connect(int arg_count, AngaraObject* args) {
    const char* host = "localhost";
    const char* user = "root";
    const char* pass = "";
    const char* db   = "";
    int port = 3306;

    if (arg_count >= 1 && IS_STR(args[0])) host = ang_api->as_cstr(args[0]);
    if (arg_count >= 2 && IS_STR(args[1])) user = ang_api->as_cstr(args[1]);
    if (arg_count >= 3 && IS_STR(args[2])) pass = ang_api->as_cstr(args[2]);
    if (arg_count >= 4 && IS_STR(args[3])) db   = ang_api->as_cstr(args[3]);
    if (arg_count >= 5 && ang_is_i64(args[4])) port = (int)ang_as_i64(args[4]);

    MYSQL* conn = mysql_init(NULL);
    if (!conn) {
        ang_api->throw_error("mysql.connect: mysql_init failed.");
        return ang_nil();
    }

    if (!mysql_real_connect(conn, host, user, pass, db, port, NULL, 0)) {
        char buf[512];
        snprintf(buf, sizeof(buf), "mysql.connect: %s", mysql_error(conn));
        mysql_close(conn);
        free(conn);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    MyConn* m = (MyConn*)calloc(1, sizeof(MyConn));
    m->conn = conn;
    return ang_api->native_instance_new(m, finalize_my, "MyConn");
}

/* build a SQL string with ? placeholders replaced by escaped values.
   returns a malloc'd string; caller frees.  throws on error and returns NULL. */
static char* mysql_interpolate(MYSQL* conn, const char* sql, AngaraObject params) {
    if (ang_is_nil(params) || !IS_LIST(params))
        return strdup(sql);

    size_t n_params = ang_api->list_len(params);
    size_t sql_len  = strlen(sql);
    size_t cap = sql_len + n_params * 64 + 1;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;

    size_t j = 0;
    size_t param_idx = 0;
    for (size_t i = 0; i < sql_len; i++) {
        if (sql[i] == '?' && param_idx < n_params) {
            AngaraObject v = ang_api->list_get(params, (int64_t)param_idx++);

            if (ang_is_nil(v)) {
                if (j + 5 >= cap) { cap = j * 2 + 5; out = (char*)realloc(out, cap); }
                memcpy(out + j, "NULL", 4); j += 4;
            } else if (ang_is_i64(v)) {
                char buf[32];
                int len = snprintf(buf, sizeof(buf), "%lld", (long long)ang_as_i64(v));
                if (j + (size_t)len + 1 >= cap) { cap = j * 2 + (size_t)len + 1; out = (char*)realloc(out, cap); }
                memcpy(out + j, buf, (size_t)len); j += (size_t)len;
            } else if (ang_is_f64(v)) {
                char buf[64];
                int len = snprintf(buf, sizeof(buf), "%.17g", ang_as_f64(v));
                if (j + (size_t)len + 1 >= cap) { cap = j * 2 + (size_t)len + 1; out = (char*)realloc(out, cap); }
                memcpy(out + j, buf, (size_t)len); j += (size_t)len;
            } else if (ang_is_bool(v)) {
                if (j + 2 >= cap) { cap = j * 2 + 2; out = (char*)realloc(out, cap); }
                out[j++] = ang_as_bool(v) ? '1' : '0';
            } else {
                /* string or other → escape */
                const char* raw;
                size_t raw_len;
                char* to_free = NULL;
                if (IS_STR(v)) {
                    raw = ang_api->as_cstr(v);
                    raw_len = ang_api->str_len(v);
                } else {
                    AngaraObject s = ang_api->to_string(v);
                    raw = ang_api->as_cstr(s);
                    raw_len = ang_api->str_len(s);
                    to_free = (char*)raw;  /* will be freed after escape */
                }
                char* escaped = (char*)malloc(raw_len * 2 + 4);
                unsigned long escaped_len = mysql_real_escape_string(conn, escaped, raw, (unsigned long)raw_len);
                escaped[escaped_len] = '\0';
                out[j++] = '\'';
                if (j + escaped_len + 2 >= cap) {
                    cap = j + escaped_len + 2;
                    out = (char*)realloc(out, cap);
                }
                memcpy(out + j, escaped, escaped_len); j += escaped_len;
                out[j++] = '\'';
                free(escaped);
                if (to_free) { /* nothing to free — to_string result is an AngaraObject, not malloc'd */ }
            }
            ang_api->decref(v);
        } else {
            if (j + 1 >= cap) { cap = j * 2 + 1; out = (char*)realloc(out, cap); }
            out[j++] = sql[i];
        }
    }
    out[j] = '\0';
    return out;
}

/* run a query and return rows as a list of records */
static AngaraObject mysql_run_query(MYSQL* conn, const char* sql, AngaraObject params) {
    char* interpolated = mysql_interpolate(conn, sql, params);
    if (!interpolated) {
        ang_api->throw_error("mysql: out of memory.");
        return ang_nil();
    }

    if (mysql_query(conn, interpolated) != 0) {
        char buf[512];
        snprintf(buf, sizeof(buf), "mysql: %s", mysql_error(conn));
        free(interpolated);
        ang_api->throw_error(buf);
        return ang_nil();
    }
    free(interpolated);

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        /* no result set — might be INSERT/UPDATE/DELETE */
        if (mysql_field_count(conn) == 0) {
            /* non-SELECT query; return affected rows */
            int64_t affected = (int64_t)mysql_affected_rows(conn);
            return ang_i64(affected);
        }
        /* error */
        char buf[512];
        snprintf(buf, sizeof(buf), "mysql: %s", mysql_error(conn));
        ang_api->throw_error(buf);
        return ang_nil();
    }

    int n_cols = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    AngaraObject rows = ang_api->list_new();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        AngaraObject rec = ang_api->record_new();
        for (int c = 0; c < n_cols; c++) {
            const char* col_name = fields[c].name ? fields[c].name : "";
            AngaraObject val;
            if (!row[c]) {
                val = ang_nil();
            } else {
                /* try integer / float, fall back to string */
                char* end;
                long long ll = strtoll(row[c], &end, 10);
                if (end == row[c] + lengths[c] && *row[c])
                    val = ang_i64((int64_t)ll);
                else {
                    double d = strtod(row[c], &end);
                    if (end == row[c] + lengths[c] && *row[c])
                        val = ang_f64(d);
                    else
                        val = ang_api->string_len(row[c], lengths[c]);
                }
            }
            ang_api->record_set(rec, col_name, val);
            ang_api->decref(val);
        }
        ang_api->list_push(rows, rec);
        ang_api->decref(rec);
    }
    mysql_free_result(result);
    return rows;
}

AngaraObject Angara_MyConn_query(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("mysql.query(sql, params?) expects a string.");
        return ang_nil();
    }
    MyConn* m = (MyConn*)ang_api->native_instance_data(args[0]);
    if (!m || !m->conn) { ang_api->throw_error("mysql.query: connection is closed."); return ang_nil(); }

    AngaraObject params = (arg_count >= 3) ? args[2] : ang_nil();
    AngaraObject result = mysql_run_query(m->conn, ang_api->as_cstr(args[1]), params);
    /* if result is i64 (affected rows from non-SELECT), wrap in a list */
    if (ang_is_i64(result)) {
        AngaraObject list = ang_api->list_new();
        ang_api->list_push(list, result);
        ang_api->decref(result);
        return list;
    }
    return result;
}

AngaraObject Angara_MyConn_query_one(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) return ang_nil();
    MyConn* m = (MyConn*)ang_api->native_instance_data(args[0]);
    if (!m || !m->conn) return ang_nil();

    AngaraObject params = (arg_count >= 3) ? args[2] : ang_nil();
    AngaraObject result = mysql_run_query(m->conn, ang_api->as_cstr(args[1]), params);
    if (ang_is_i64(result)) { ang_api->decref(result); return ang_nil(); }
    if (IS_LIST(result)) {
        size_t len = ang_api->list_len(result);
        if (len == 0) { ang_api->decref(result); return ang_nil(); }
        AngaraObject first = ang_api->list_get(result, 0);
        ang_api->decref(result);
        return first;
    }
    return result;
}

AngaraObject Angara_MyConn_execute(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("mysql.execute(sql, params?) expects a string.");
        return ang_nil();
    }
    MyConn* m = (MyConn*)ang_api->native_instance_data(args[0]);
    if (!m || !m->conn) { ang_api->throw_error("mysql.execute: connection is closed."); return ang_nil(); }

    AngaraObject params = (arg_count >= 3) ? args[2] : ang_nil();
    AngaraObject result = mysql_run_query(m->conn, ang_api->as_cstr(args[1]), params);
    if (ang_is_i64(result)) return result;  /* affected rows */
    /* was a SELECT — return row count */
    int64_t n = 0;
    if (IS_LIST(result)) { n = (int64_t)ang_api->list_len(result); ang_api->decref(result); }
    return ang_i64(n);
}

AngaraObject Angara_MyConn_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    MyConn* m = (MyConn*)ang_api->native_instance_data(args[0]);
    if (m && m->conn) {
        mysql_close(m->conn);
        free(m->conn);
        m->conn = NULL;
    }
    return ang_nil();
}


static const AngaraMethodDef MY_METHODS[] = {
    {"query",      (AngaraMethodFn)Angara_MyConn_query,      "sl<a>?->l<{}>"},
    {"query_one",  (AngaraMethodFn)Angara_MyConn_query_one,  "sl<a>?->{}?"},
    {"execute",    (AngaraMethodFn)Angara_MyConn_execute,    "sl<a>?->i"},
    {"close",      (AngaraMethodFn)Angara_MyConn_close,      "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef MY_CLASS = { "MyConn", NULL, MY_METHODS };

static const AngaraFuncDef MYSQL_EXPORTS[] = {
    {"connect", Angara_mysql_connect, "ssssi?->MyConn", &MY_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(mysql) {
    ang_api = api;
    *def_count = (sizeof(MYSQL_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return MYSQL_EXPORTS;
}

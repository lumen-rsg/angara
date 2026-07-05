/// Angara PostgreSQL module — libpq-based client.
///
///   let db = postgres.connect("host=localhost dbname=mydb")
///   let rows = db.query("SELECT id, name FROM users WHERE id = $1", [42])
///   db.execute("INSERT INTO logs (msg) VALUES ($1)", ["hello"])
///   db.close()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

typedef struct {
    PGconn* conn;
} PgConn;

static void finalize_pg(void* data) {
    PgConn* p = (PgConn*)data;
    if (p->conn) PQfinish(p->conn);
    free(p);
}

AngaraObject Angara_postgres_connect(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("postgres.connect(conninfo) expects a string.");
        return ang_nil();
    }
    const char* conninfo = ang_api->as_cstr(args[0]);

    PGconn* conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "postgres.connect: %s", PQerrorMessage(conn));
        PQfinish(conn);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    PgConn* p = (PgConn*)calloc(1, sizeof(PgConn));
    p->conn = conn;
    return ang_api->native_instance_new(p, finalize_pg, "PgConn");
}

/* bind parameters and execute; returns PGresult* or NULL on error (throws). */
static PGresult* pg_exec_params(PGconn* conn, const char* sql, AngaraObject params) {
    size_t n_params = 0;
    if (!ang_is_nil(params) && IS_LIST(params))
        n_params = ang_api->list_len(params);

    if (n_params == 0) {
        PGresult* res = PQexec(conn, sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK &&
            PQresultStatus(res) != PGRES_TUPLES_OK) {
            char buf[512];
            snprintf(buf, sizeof(buf), "postgres: %s", PQerrorMessage(conn));
            PQclear(res);
            ang_api->throw_error(buf);
            return NULL;
        }
        return res;
    }

    /* build param arrays */
    char** param_values = (char**)malloc(n_params * sizeof(char*));
    int*   param_lengths = (int*)malloc(n_params * sizeof(int));
    int*   param_formats = (int*)malloc(n_params * sizeof(int));
    /* we need to keep string copies alive until PQexecParams returns */
    char** string_copies = (char**)calloc(n_params, sizeof(char*));

    for (size_t i = 0; i < n_params; i++) {
        AngaraObject v = ang_api->list_get(params, (int64_t)i);
        param_formats[i] = 0;  /* text */

        if (ang_is_nil(v)) {
            param_values[i] = NULL;
            param_lengths[i] = 0;
        } else if (ang_is_i64(v)) {
            char* s = (char*)malloc(32);
            int len = snprintf(s, 32, "%lld", (long long)ang_as_i64(v));
            param_values[i] = s;
            param_lengths[i] = len;
            string_copies[i] = s;
        } else if (ang_is_f64(v)) {
            char* s = (char*)malloc(64);
            int len = snprintf(s, 64, "%.17g", ang_as_f64(v));
            param_values[i] = s;
            param_lengths[i] = len;
            string_copies[i] = s;
        } else if (ang_is_bool(v)) {
            param_values[i] = ang_as_bool(v) ? (char*)"t" : (char*)"f";
            param_lengths[i] = 1;
        } else if (IS_STR(v)) {
            param_values[i] = (char*)ang_api->as_cstr(v);
            param_lengths[i] = (int)ang_api->str_len(v);
        } else {
            AngaraObject s = ang_api->to_string(v);
            const char* cs = ang_api->as_cstr(s);
            size_t slen = ang_api->str_len(s);
            char* copy = (char*)malloc(slen + 1);
            memcpy(copy, cs, slen); copy[slen] = '\0';
            param_values[i] = copy;
            param_lengths[i] = (int)slen;
            string_copies[i] = copy;
            ang_api->decref(s);
        }
        ang_api->decref(v);
    }

    PGresult* res = PQexecParams(conn, sql, (int)n_params, NULL,
                                 (const char* const*)param_values,
                                 param_lengths, param_formats, 0);

    /* free string copies */
    for (size_t i = 0; i < n_params; i++) free(string_copies[i]);
    free(string_copies);
    free(param_values);
    free(param_lengths);
    free(param_formats);

    if (PQresultStatus(res) != PGRES_COMMAND_OK &&
        PQresultStatus(res) != PGRES_TUPLES_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "postgres: %s", PQerrorMessage(conn));
        PQclear(res);
        ang_api->throw_error(buf);
        return NULL;
    }
    return res;
}

/* convert a PGresult row/col to AngaraObject */
static AngaraObject pg_cell_to_angara(PGresult* res, int row, int col) {
    if (PQgetisnull(res, row, col)) return ang_nil();
    char* val = PQgetvalue(res, row, col);
    int len = PQgetlength(res, row, col);
    /* try to parse as integer if it looks like one */
    char* end;
    long long ll = strtoll(val, &end, 10);
    if (end == val + len && *val) return ang_i64((int64_t)ll);
    /* try float */
    double d = strtod(val, &end);
    if (end == val + len && *val) return ang_f64(d);
    /* default to string */
    return ang_api->string_len(val, (size_t)len);
}

AngaraObject Angara_PgConn_query(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("pg.query(sql, params?) expects a string.");
        return ang_nil();
    }
    PgConn* p = (PgConn*)ang_api->native_instance_data(args[0]);
    if (!p || !p->conn) { ang_api->throw_error("pg.query: connection is closed."); return ang_nil(); }

    const char* sql = ang_api->as_cstr(args[1]);
    AngaraObject params = (arg_count >= 3) ? args[2] : ang_nil();

    PGresult* res = pg_exec_params(p->conn, sql, params);
    if (!res) return ang_nil();  /* error already thrown */

    int n_rows = PQntuples(res);
    int n_cols = PQnfields(res);

    AngaraObject rows = ang_api->list_new();
    for (int r = 0; r < n_rows; r++) {
        AngaraObject row = ang_api->record_new();
        for (int c = 0; c < n_cols; c++) {
            const char* col_name = PQfname(res, c);
            AngaraObject val = pg_cell_to_angara(res, r, c);
            ang_api->record_set(row, col_name ? col_name : "", val);
            ang_api->decref(val);
        }
        ang_api->list_push(rows, row);
        ang_api->decref(row);
    }
    PQclear(res);
    return rows;
}

AngaraObject Angara_PgConn_query_one(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) return ang_nil();
    PgConn* p = (PgConn*)ang_api->native_instance_data(args[0]);
    if (!p || !p->conn) return ang_nil();

    const char* sql = ang_api->as_cstr(args[1]);
    AngaraObject params = (arg_count >= 3) ? args[2] : ang_nil();

    PGresult* res = pg_exec_params(p->conn, sql, params);
    if (!res) return ang_nil();

    if (PQntuples(res) == 0) { PQclear(res); return ang_nil(); }

    int n_cols = PQnfields(res);
    AngaraObject row = ang_api->record_new();
    for (int c = 0; c < n_cols; c++) {
        const char* col_name = PQfname(res, c);
        AngaraObject val = pg_cell_to_angara(res, 0, c);
        ang_api->record_set(row, col_name ? col_name : "", val);
        ang_api->decref(val);
    }
    PQclear(res);
    return row;
}

AngaraObject Angara_PgConn_execute(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("pg.execute(sql, params?) expects a string.");
        return ang_nil();
    }
    PgConn* p = (PgConn*)ang_api->native_instance_data(args[0]);
    if (!p || !p->conn) { ang_api->throw_error("pg.execute: connection is closed."); return ang_nil(); }

    const char* sql = ang_api->as_cstr(args[1]);
    AngaraObject params = (arg_count >= 3) ? args[2] : ang_nil();

    PGresult* res = pg_exec_params(p->conn, sql, params);
    if (!res) return ang_nil();

    int64_t affected = (int64_t)atoi(PQcmdTuples(res));
    PQclear(res);
    return ang_i64(affected);
}

AngaraObject Angara_PgConn_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    PgConn* p = (PgConn*)ang_api->native_instance_data(args[0]);
    if (p && p->conn) {
        PQfinish(p->conn);
        p->conn = NULL;
    }
    return ang_nil();
}


static const AngaraMethodDef PG_METHODS[] = {
    {"query",      (AngaraMethodFn)Angara_PgConn_query,      "sl<a>?->l<{}>"},
    {"query_one",  (AngaraMethodFn)Angara_PgConn_query_one,  "sl<a>?->{}?"},
    {"execute",    (AngaraMethodFn)Angara_PgConn_execute,    "sl<a>?->i"},
    {"close",      (AngaraMethodFn)Angara_PgConn_close,      "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef PG_CLASS = { "PgConn", NULL, PG_METHODS };

static const AngaraFuncDef PG_EXPORTS[] = {
    {"connect", Angara_postgres_connect, "s->PgConn", &PG_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(postgres) {
    ang_api = api;
    *def_count = (sizeof(PG_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return PG_EXPORTS;
}

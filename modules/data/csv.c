/// Angara CSV module — parse and stringify with quoting, delimiters, and header-based records.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

typedef struct {
    const char* data;
    size_t      len;
    size_t      pos;
    char        delimiter;
} CsvParser;

static char parser_peek(CsvParser* p) {
    if (p->pos >= p->len) return '\0';
    return p->data[p->pos];
}

static char parser_next(CsvParser* p) {
    if (p->pos >= p->len) return '\0';
    return p->data[p->pos++];
}

static char* parse_field(CsvParser* p) {
    size_t cap = 128;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;

    if (parser_peek(p) == '"') {
        parser_next(p);
        while (p->pos < p->len) {
            char c = parser_next(p);
            if (c == '"') {
                if (parser_peek(p) == '"') {
                    parser_next(p);
                    if (len + 1 >= cap) { cap *= 2; char* nb = realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
                    buf[len++] = '"';
                } else {
                    break;
                }
            } else {
                if (len + 1 >= cap) { cap *= 2; char* nb = realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
                buf[len++] = c;
            }
        }
    } else {
        while (p->pos < p->len) {
            char c = parser_peek(p);
            if (c == p->delimiter || c == '\r' || c == '\n') break;
            if (len + 1 >= cap) { cap *= 2; char* nb = realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
            buf[len++] = parser_next(p);
        }
    }

    buf[len] = '\0';
    return buf;
}

static AngaraObject parse_row(CsvParser* p) {
    AngaraObject row = ang_api->list_new();

    while (1) {
        char* field = parse_field(p);
        if (field) {
            ang_api->list_push(row, ang_api->string_no_copy(field, strlen(field)));
        } else {
            ang_api->list_push(row, ang_api->string(""));
        }

        char c = parser_peek(p);
        if (c == p->delimiter) {
            parser_next(p);
            char next = parser_peek(p);
            if (next == '\r' || next == '\n' || next == '\0') {
                ang_api->list_push(row, ang_api->string(""));
                break;
            }
            continue;
        }
        break;
    }

    if (parser_peek(p) == '\r') {
        parser_next(p);
        if (parser_peek(p) == '\n') parser_next(p);
    } else if (parser_peek(p) == '\n') {
        parser_next(p);
    }

    return row;
}

AngaraObject Angara_csv_parse(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("csv.parse(text, delimiter?) expects a string and optional delimiter.");
        return ang_nil();
    }

    char delim = ',';
    if (arg_count >= 2 && IS_STR(args[1]) && ang_api->str_len(args[1]) > 0) {
        delim = ang_api->as_cstr(args[1])[0];
    }

    CsvParser parser = {
        .data = ang_api->as_cstr(args[0]),
        .len = ang_api->str_len(args[0]),
        .pos = 0,
        .delimiter = delim
    };

    AngaraObject result = ang_api->list_new();

    while (parser.pos < parser.len) {
        AngaraObject row = parse_row(&parser);
        ang_api->list_push(result, row);
        ang_api->decref(row);
    }

    return result;
}

AngaraObject Angara_csv_parse_record(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("csv.parse_record(text, delimiter?) expects a string and optional delimiter.");
        return ang_nil();
    }

    char delim = ',';
    if (arg_count >= 2 && IS_STR(args[1]) && ang_api->str_len(args[1]) > 0) {
        delim = ang_api->as_cstr(args[1])[0];
    }

    CsvParser parser = {
        .data = ang_api->as_cstr(args[0]),
        .len = ang_api->str_len(args[0]),
        .pos = 0,
        .delimiter = delim
    };

    AngaraObject result = ang_api->list_new();

    if (parser.pos >= parser.len) return result;
    AngaraObject headers = parse_row(&parser);
    size_t num_headers = ang_api->list_len(headers);

    while (parser.pos < parser.len) {
        AngaraObject row = parse_row(&parser);
        size_t num_fields = ang_api->list_len(row);

        AngaraObject rec = ang_api->record_new();
        for (size_t i = 0; i < num_headers && i < num_fields; i++) {
            AngaraObject key_obj = ang_api->list_get(headers, (int64_t)i);
            AngaraObject val_obj = ang_api->list_get(row, (int64_t)i);
            const char* key = ang_api->as_cstr(key_obj);
            ang_api->record_set(rec, key, val_obj);
            ang_api->decref(key_obj);
            ang_api->decref(val_obj);
        }

        ang_api->list_push(result, rec);
        ang_api->decref(row);
        ang_api->decref(rec);
    }

    ang_api->decref(headers);
    return result;
}

AngaraObject Angara_csv_stringify(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_LIST(args[0])) {
        ang_api->throw_error("csv.stringify(rows, delimiter?) expects a list of lists.");
        return ang_nil();
    }

    char delim = ',';
    if (arg_count >= 2 && IS_STR(args[1]) && ang_api->str_len(args[1]) > 0) {
        delim = ang_api->as_cstr(args[1])[0];
    }

    size_t buf_cap = 4096;
    size_t buf_len = 0;
    char* buf = (char*)malloc(buf_cap);
    if (!buf) { ang_api->throw_error("csv.stringify: out of memory."); return ang_nil(); }

    #define EMIT(ch) do { \
        if (buf_len + 2 >= buf_cap) { buf_cap *= 2; char* nb = realloc(buf, buf_cap); \
          if (!nb) { free(buf); ang_api->throw_error("csv.stringify: out of memory."); return ang_nil(); } buf = nb; } \
        buf[buf_len++] = (ch); \
    } while(0)

    size_t num_rows = ang_api->list_len(args[0]);
    for (size_t r = 0; r < num_rows; r++) {
        AngaraObject row = ang_api->list_get(args[0], (int64_t)r);
        if (!ang_is_obj(row) || ang_api->obj_type(row) != ANG_OBJ_LIST) {
            ang_api->decref(row);
            continue;
        }

        size_t num_fields = ang_api->list_len(row);
        for (size_t f = 0; f < num_fields; f++) {
            AngaraObject field = ang_api->list_get(row, (int64_t)f);
            const char* val = "";
            size_t val_len = 0;

            if (IS_STR(field)) {
                val = ang_api->as_cstr(field);
                val_len = ang_api->str_len(field);
            }

            int needs_quote = 0;
            for (size_t i = 0; i < val_len; i++) {
                if (val[i] == delim || val[i] == '"' || val[i] == '\n' || val[i] == '\r') {
                    needs_quote = 1;
                    break;
                }
            }

            if (needs_quote) EMIT('"');
            for (size_t i = 0; i < val_len; i++) {
                if (val[i] == '"') EMIT('"');
                EMIT(val[i]);
            }
            if (needs_quote) EMIT('"');

            if (f + 1 < num_fields) EMIT(delim);
            ang_api->decref(field);
        }

        if (r + 1 < num_rows) EMIT('\n');
        ang_api->decref(row);
    }

    #undef EMIT

    buf[buf_len] = '\0';
    return ang_api->string_no_copy(buf, buf_len);
}

AngaraObject Angara_csv_read_file(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("csv.read_file(path, delimiter?) expects a path string.");
        return ang_nil();
    }

    const char* path = ang_api->as_cstr(args[0]);
    FILE* file = fopen(path, "rb");
    if (!file) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "csv.read_file: cannot open '%s'", path);
        ang_api->throw_error(errbuf);
        return ang_nil();
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = (size_t)ftell(file);
    rewind(file);

    char* content = (char*)malloc(file_size + 1);
    if (!content) { fclose(file); ang_api->throw_error("csv.read_file: out of memory."); return ang_nil(); }
    fread(content, 1, file_size, file);
    content[file_size] = '\0';
    fclose(file);

    char delim = ',';
    if (arg_count >= 2 && IS_STR(args[1]) && ang_api->str_len(args[1]) > 0) {
        delim = ang_api->as_cstr(args[1])[0];
    }

    CsvParser parser = {
        .data = content,
        .len = file_size,
        .pos = 0,
        .delimiter = delim
    };

    AngaraObject result = ang_api->list_new();
    while (parser.pos < parser.len) {
        AngaraObject row = parse_row(&parser);
        ang_api->list_push(result, row);
        ang_api->decref(row);
    }

    free(content);
    return result;
}

AngaraObject Angara_csv_write_file(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[0]) || !IS_LIST(args[1])) {
        ang_api->throw_error("csv.write_file(path, rows, delimiter?) expects a path and a list of lists.");
        return ang_nil();
    }

    AngaraObject csv_text = Angara_csv_stringify(arg_count >= 2 ? arg_count - 1 : 1, args + 1);
    if (!IS_STR(csv_text)) return ang_nil();

    const char* path = ang_api->as_cstr(args[0]);
    FILE* file = fopen(path, "w");
    if (!file) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "csv.write_file: cannot open '%s'", path);
        ang_api->throw_error(errbuf);
        ang_api->decref(csv_text);
        return ang_nil();
    }

    const char* text = ang_api->as_cstr(csv_text);
    size_t text_len = ang_api->str_len(csv_text);
    fwrite(text, 1, text_len, file);
    fputc('\n', file);
    fclose(file);
    ang_api->decref(csv_text);
    return ang_nil();
}

static const AngaraFuncDef CSV_EXPORTS[] = {
    {"parse",         Angara_csv_parse,         "ss?->l<l<s>>", NULL},
    {"parse_record",  Angara_csv_parse_record,  "ss?->l<{}>",   NULL},
    {"stringify",     Angara_csv_stringify,     "l<l<s>>s?->s", NULL},
    {"read_file",     Angara_csv_read_file,     "ss?->l<l<s>>", NULL},
    {"write_file",    Angara_csv_write_file,    "sl<l<s>>s?->n", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(csv) {
    ang_api = api;
    *def_count = (sizeof(CSV_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return CSV_EXPORTS;
}
/// Angara I/O module — stdin/stdout/stderr read and write operations.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Angara.h"

AngaraObject Angara_io_write(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_i64(args[0]) || !ang_is_obj(args[1])) {
        ang_api->throw_error("write(stream_id, content) expects an integer and a string.");
        return ang_nil();
    }

    int64_t stream_id = ang_as_i64(args[0]);
    const char* content = ang_api->as_cstr(args[1]);

    FILE* stream = NULL;
    if (stream_id == 1) {
        stream = stdout;
    } else if (stream_id == 2) {
        stream = stderr;
    } else {
        ang_api->throw_error("Invalid stream ID for write(). Use 1 for stdout or 2 for stderr.");
        return ang_nil();
    }

    fprintf(stream, "%s", content);
    return ang_nil();
}

AngaraObject Angara_io_println(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_i64(args[0]) || !ang_is_obj(args[1])) {
        ang_api->throw_error("println(stream_id, content) expects an integer and a string.");
        return ang_nil();
    }

    int64_t stream_id = ang_as_i64(args[0]);
    const char* content = ang_api->as_cstr(args[1]);

    FILE* stream = NULL;
    if (stream_id == 1) {
        stream = stdout;
    } else if (stream_id == 2) {
        stream = stderr;
    } else {
        ang_api->throw_error("Invalid stream ID for println(). Use 1 for stdout or 2 for stderr.");
        return ang_nil();
    }

    fputs(content, stream);
    fputc('\n', stream);
    return ang_nil();
}

AngaraObject Angara_io_flush(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("flush(stream_id) expects an integer stream ID.");
        return ang_nil();
    }

    int64_t stream_id = ang_as_i64(args[0]);
    if (stream_id == 1) fflush(stdout);
    else if (stream_id == 2) fflush(stderr);

    return ang_nil();
}

AngaraObject Angara_io_read_line(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        ang_api->throw_error("read_line() expects no arguments.");
        return ang_nil();
    }

    char* line_buf = NULL;
    size_t line_buf_size = 0;
    ssize_t line_size = getline(&line_buf, &line_buf_size, stdin);

    if (line_size < 0) {
        free(line_buf);
        return ang_nil();
    }

    if (line_size > 0 && line_buf[line_size - 1] == '\n') {
        line_buf[line_size - 1] = '\0';
        line_size--;
    }

    AngaraObject result = ang_api->string_len(line_buf, (size_t)line_size);
    free(line_buf);
    return result;
}

AngaraObject Angara_io_read_all(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        ang_api->throw_error("read_all() expects no arguments.");
        return ang_nil();
    }

    size_t capacity = 4096;
    size_t total_read = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        ang_api->throw_error("Failed to allocate memory in read_all().");
        return ang_nil();
    }

    size_t bytes_read;
    while ((bytes_read = fread(buffer + total_read, 1, capacity - total_read, stdin)) > 0) {
        total_read += bytes_read;
        if (total_read == capacity) {
            capacity *= 2;
            char* new_buffer = (char*)realloc(buffer, capacity);
            if (!new_buffer) {
                free(buffer);
                ang_api->throw_error("Failed to reallocate memory in read_all().");
                return ang_nil();
            }
            buffer = new_buffer;
        }
    }

    buffer[total_read] = '\0';
    AngaraObject result = ang_api->string_len(buffer, total_read);
    free(buffer);
    return result;
}

AngaraObject Angara_io_print(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0])) {
        ang_api->throw_error("print(content) expects one string argument.");
        return ang_nil();
    }
    fprintf(stdout, "%s", ang_api->as_cstr(args[0]));
    return ang_nil();
}

AngaraObject Angara_io_println_auto(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0])) {
        ang_api->throw_error("println_auto(content) expects one string argument.");
        return ang_nil();
    }
    fputs(ang_api->as_cstr(args[0]), stdout);
    fputc('\n', stdout);
    return ang_nil();
}

AngaraObject Angara_io_eprint(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0])) {
        ang_api->throw_error("eprint(content) expects one string argument.");
        return ang_nil();
    }
    fprintf(stderr, "%s", ang_api->as_cstr(args[0]));
    return ang_nil();
}

AngaraObject Angara_io_eprintln(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0])) {
        ang_api->throw_error("eprintln(content) expects one string argument.");
        return ang_nil();
    }
    fputs(ang_api->as_cstr(args[0]), stderr);
    fputc('\n', stderr);
    return ang_nil();
}

static const AngaraFuncDef IO_EXPORTS[] = {
    {"write",             Angara_io_write,             "is->n",          NULL},
    {"println",           Angara_io_println,           "is->n",          NULL},
    {"flush",             Angara_io_flush,             "i->n",           NULL},
    {"read_line",         Angara_io_read_line,         "->s",            NULL},
    {"read_all",          Angara_io_read_all,          "->s",            NULL},
    {"print",             Angara_io_print,             "s->n",           NULL},
    {"println_auto",      Angara_io_println_auto,      "s->n",           NULL},
    {"eprint",            Angara_io_eprint,            "s->n",           NULL},
    {"eprintln",          Angara_io_eprintln,          "s->n",           NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(io) {
    ang_api = api;
    *def_count = (sizeof(IO_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return IO_EXPORTS;
}
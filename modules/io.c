//
// Created by cv2 on 9/11/25.
//


#include "AngaraABI.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Function Implementations ---

// Writes content to a specific stream (stdout or stderr).
AngaraObject Angara_io_write(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ANGARA_IS_I64(args[0]) || !ANGARA_IS_STRING(args[1])) {
        angara_throw_error("write(stream_id, content) expects an integer and a string.");
        return angara_create_nil();
    }

    int64_t stream_id = ANGARA_AS_I64(args[0]);
    const char* content = ANGARA_AS_CSTRING(args[1]);

    FILE* stream = NULL;
    if (stream_id == 1) {
        stream = stdout;
    } else if (stream_id == 2) {
        stream = stderr;
    } else {
        angara_throw_error("Invalid stream ID for write(). Use 1 for stdout or 2 for stderr.");
        return angara_create_nil();
    }

    fprintf(stream, "%s", content);
    return angara_create_nil();
}

// Flushes a stream's buffer.
AngaraObject Angara_io_flush(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ANGARA_IS_I64(args[0])) {
        angara_throw_error("flush(stream_id) expects an integer stream ID.");
        return angara_create_nil();
    }
    int64_t stream_id = ANGARA_AS_I64(args[0]);

    if (stream_id == 1) {
        fflush(stdout);
    } else if (stream_id == 2) {
        fflush(stderr);
    } // Flushing stdin is not meaningful

    return angara_create_nil();
}

// Reads one line from stdin. Returns nil on EOF.
AngaraObject Angara_io_read_line(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        angara_throw_error("read_line() expects no arguments.");
        return angara_create_nil();
    }

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    // getline is a POSIX function that safely allocates memory.
    ssize_t line_size = getline(&line_buf, &line_buf_size, stdin);

    if (line_size < 0) {
        free(line_buf); // Must free even on failure
        return angara_create_nil(); // Return nil for EOF or error
    }

    // Strip the trailing newline character, if it exists.
    if (line_size > 0 && line_buf[line_size - 1] == '\n') {
        line_buf[line_size - 1] = '\0';
        line_size--;
    }

    // We can transfer ownership of the malloc'd buffer to Angara.
    return angara_create_string_no_copy(line_buf, line_size);
}

// Reads all of stdin until EOF.
AngaraObject Angara_io_read_all(int arg_count, AngaraObject* args) {
    if (arg_count != 0) {
        angara_throw_error("read_all() expects no arguments.");
        return angara_create_nil();
    }

    size_t capacity = 4096; // Start with a 4KB buffer
    size_t total_read = 0;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        angara_throw_error("Failed to allocate memory in read_all().");
        return angara_create_nil();
    }

    size_t bytes_read;
    while ((bytes_read = fread(buffer + total_read, 1, capacity - total_read, stdin)) > 0) {
        total_read += bytes_read;
        if (total_read == capacity) {
            capacity *= 2; // Double the buffer size
            char* new_buffer = (char*)realloc(buffer, capacity);
            if (!new_buffer) {
                free(buffer);
                angara_throw_error("Failed to reallocate memory in read_all().");
                return angara_create_nil();
            }
            buffer = new_buffer;
        }
    }

    buffer[total_read] = '\0';
    return angara_create_string_no_copy(buffer, total_read);
}


// --- Module Definition ---

static const AngaraFuncDef IO_FUNCTIONS[] = {
        // name,         c_function,             arity, type_string
        {"write",        Angara_io_write,        2,     "is->n"}, // int, string -> nil
        {"flush",        Angara_io_flush,        1,     "i->n"},  // int -> nil
        {"read_line",    Angara_io_read_line,    0,     "->s"},   // () -> string (can be nil)
        {"read_all",     Angara_io_read_all,     0,     "->s"},   // () -> string
        {NULL, NULL, 0, NULL}
};

ANGARA_MODULE_INIT(io) {
        *def_count = (sizeof(IO_FUNCTIONS) / sizeof(AngaraFuncDef)) - 1;
        return IO_FUNCTIONS;
}
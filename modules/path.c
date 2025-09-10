//
// Created by cv2 on 9/10/25.
//

#include "AngaraABI.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Use the platform's preferred separator. For simplicity, we'll hardcode '/'
// which works on Linux, macOS, and even modern Windows C libraries.
const char PLATFORM_SEPARATOR = '/';

// --- Function Implementations ---

// Joins multiple path components. This is a variadic function.
AngaraObject Angara_path_join(int arg_count, AngaraObject* args) {
    if (arg_count == 0) {
        return angara_string_from_c(".");
    }

    size_t total_len = 0;
    // First pass: calculate the total length needed for the final string.
    for (int i = 0; i < arg_count; i++) {
        if (!ANGARA_IS_STRING(args[i])) {
            angara_throw_error("all arguments to path.join() must be strings.");
            return angara_create_nil();
        }
        total_len += ((AngaraString*)args[i].as.obj)->length;
    }
    // Add space for separators and the null terminator.
    total_len += (arg_count - 1) + 1;

    char* result_buf = (char*)malloc(total_len);
    if (!result_buf) {
        angara_throw_error("Failed to allocate memory in path.join().");
        return angara_create_nil();
    }
    result_buf[0] = '\0'; // Start with an empty string

    // Second pass: build the final string.
    char* current_pos = result_buf;
    for (int i = 0; i < arg_count; i++) {
        const char* part = ANGARA_AS_CSTRING(args[i]);
        size_t part_len = ((AngaraString*)args[i].as.obj)->length;

        // Don't prepend a separator for the very first part.
        if (i > 0) {
            *current_pos = PLATFORM_SEPARATOR;
            current_pos++;
        }

        memcpy(current_pos, part, part_len);
        current_pos += part_len;
    }
    *current_pos = '\0';

    return angara_create_string_no_copy(result_buf, total_len - 1);
}

// Gets the final component of a path (e.g., "c.txt" from "/a/b/c.txt").
AngaraObject Angara_path_basename(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ANGARA_IS_STRING(args[0])) {
        angara_throw_error("basename(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = ANGARA_AS_CSTRING(args[0]);

    // strrchr finds the last occurrence of a character.
    const char* last_slash = strrchr(path, PLATFORM_SEPARATOR);
    if (!last_slash) {
        // No separator found, the whole path is the basename.
        return angara_string_from_c(path);
    }
    // Return the substring starting just after the separator.
    return angara_string_from_c(last_slash + 1);
}

// Gets the directory portion of a path (e.g., "/a/b" from "/a/b/c.txt").
AngaraObject Angara_path_dirname(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ANGARA_IS_STRING(args[0])) {
        angara_throw_error("dirname(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = ANGARA_AS_CSTRING(args[0]);

    const char* last_slash = strrchr(path, PLATFORM_SEPARATOR);
    if (!last_slash) {
        // No separator, so the directory is the current directory ".".
        return angara_string_from_c(".");
    }
    if (last_slash == path) {
        // Path is something like "/file", dirname is "/".
        return angara_string_from_c("/");
    }

    size_t len = last_slash - path;
    char* dir_buf = (char*)malloc(len + 1);
    memcpy(dir_buf, path, len);
    dir_buf[len] = '\0';

    return angara_create_string_no_copy(dir_buf, len);
}

// Gets the extension of a path (e.g., ".txt" from "file.txt").
AngaraObject Angara_path_extension(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ANGARA_IS_STRING(args[0])) {
        angara_throw_error("extension(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = ANGARA_AS_CSTRING(args[0]);

    const char* last_dot = strrchr(path, '.');
    // Also check for a slash to handle cases like ".bashrc"
    const char* last_slash = strrchr(path, PLATFORM_SEPARATOR);

    // If there's no dot, or if the last slash appears after the last dot
    // (e.g. in a directory name like "archive.v1/file"), there's no extension.
    if (!last_dot || (last_slash && last_slash > last_dot)) {
        return angara_string_from_c(""); // Empty string for no extension
    }

    return angara_string_from_c(last_dot);
}


// --- Module Definition ---

static const AngaraFuncDef PATH_FUNCTIONS[] = {
        // name,      c_function,             arity, type_string
        {"join",      Angara_path_join,      -1,    "s...->s"}, // Variadic, takes strings, returns a string
        {"basename",  Angara_path_basename,   1,     "s->s"},
        {"dirname",   Angara_path_dirname,    1,     "s->s"},
        {"extension", Angara_path_extension,  1,     "s->s"},
        {NULL, NULL, 0, NULL}
};

// The official entry point for the 'path' module.
// Note the name change from fs to path.
ANGARA_MODULE_INIT(path) {
        *def_count = (sizeof(PATH_FUNCTIONS) / sizeof(AngaraFuncDef)) - 1;
        return PATH_FUNCTIONS;
}
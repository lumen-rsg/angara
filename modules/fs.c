
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>    // Required for `errno`
#include <unistd.h>   // Required for link, symlink, rmdir
#include <sys/stat.h> // Required for mkdir
#include "../src/runtime/angara_runtime.h"

// --- Helper for formatting error messages ---
// This prevents us from needing a large static buffer.
static void throw_fs_error(const char* message, const char* path) {
    // strerror(errno) gets the system's human-readable error string.
    const char* error_reason = strerror(errno);
    // We need to allocate a new buffer for the combined message.
    char* full_message = (char*)malloc(strlen(message) + strlen(path) + strlen(error_reason) + 10);
    sprintf(full_message, "%s '%s': %s", message, path, error_reason);
    angara_throw_error(full_message);
    // angara_throw_error makes a copy, so we must free our allocated buffer.
    free(full_message);
}


// --- Function Implementations ---

AngaraObject Angara_fs_read_file(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("read_file(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);

    FILE* file = fopen(path, "rb");
    if (!file) {
        // Return nil if the file simply doesn't exist (a common, non-fatal case).
        return angara_create_nil();
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        angara_throw_error("Failed to allocate memory to read file.");
        return angara_create_nil();
    }

    fread(buffer, sizeof(char), file_size, file);
    buffer[file_size] = '\0';
    fclose(file);

    return angara_create_string_no_copy(buffer, file_size);
}

AngaraObject Angara_fs_write_file(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        angara_throw_error("write_file(path, content) expects two string arguments.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);
    const char* content = AS_CSTRING(args[1]);
    size_t content_len = ((AngaraString*)args[1].as.obj)->length;

    FILE* file = fopen(path, "wb");
    if (!file) {
        throw_fs_error("Failed to open file for writing", path);
        return angara_create_nil();
    }

    size_t written = fwrite(content, sizeof(char), content_len, file);
    fclose(file);

    if (written < content_len) {
        angara_throw_error("Failed to write entire content to file.");
        return angara_create_nil();
    }

    return angara_create_nil(); // Success
}

AngaraObject Angara_fs_remove_file(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("remove_file(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);

    if (remove(path) != 0) {
        throw_fs_error("Failed to remove file", path);
        return angara_create_nil();
    }
    return angara_create_nil();
}

AngaraObject Angara_fs_create_dir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("create_dir(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);

    // 0777 are standard permissions, modified by the system's umask.
    if (mkdir(path, 0777) != 0) {
        throw_fs_error("Failed to create directory", path);
        return angara_create_nil();
    }
    return angara_create_nil();
}

AngaraObject Angara_fs_remove_dir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("remove_dir(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);

    if (rmdir(path) != 0) {
        throw_fs_error("Failed to remove directory", path);
        return angara_create_nil();
    }
    return angara_create_nil();
}

AngaraObject Angara_fs_rename_path(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        angara_throw_error("rename_path(old_path, new_path) expects two string arguments.");
        return angara_create_nil();
    }
    const char* old_path = AS_CSTRING(args[0]);
    const char* new_path = AS_CSTRING(args[1]);

    if (rename(old_path, new_path) != 0) {
        throw_fs_error("Failed to rename path", old_path);
        return angara_create_nil();
    }
    return angara_create_nil();
}

AngaraObject Angara_fs_create_symlink(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        angara_throw_error("create_symlink(target_path, link_path) expects two string arguments.");
        return angara_create_nil();
    }
    const char* target = AS_CSTRING(args[0]);
    const char* link_path = AS_CSTRING(args[1]);

    if (symlink(target, link_path) != 0) {
        throw_fs_error("Failed to create symbolic link", link_path);
        return angara_create_nil();
    }
    return angara_create_nil();
}

AngaraObject Angara_fs_create_hardlink(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        angara_throw_error("create_hardlink(target_path, link_path) expects two string arguments.");
        return angara_create_nil();
    }
    const char* target = AS_CSTRING(args[0]);
    const char* link_path = AS_CSTRING(args[1]);

    if (link(target, link_path) != 0) {
        throw_fs_error("Failed to create hard link", link_path);
        return angara_create_nil();
    }
    return angara_create_nil();
}

// Helper function to avoid duplicating stat logic.
// Returns 0 on success, -1 on failure.
static int get_stat(const char* path, struct stat* st, bool follow_symlinks) {
    if (follow_symlinks) {
        return stat(path, st);
    }
    // lstat does NOT follow the symlink, it stats the link itself.
    return lstat(path, st);
}

AngaraObject Angara_fs_exists(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("exists(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);
    struct stat st;
    // stat returns 0 if the file exists.
    return angara_create_bool(stat(path, &st) == 0);
}

AngaraObject Angara_fs_is_file(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("is_file(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);
    struct stat st;
    if (get_stat(path, &st, true) != 0) {
        return angara_create_bool(false); // Doesn't exist, so can't be a file.
    }
    // S_ISREG is a macro that checks the file mode bits.
    return angara_create_bool(S_ISREG(st.st_mode));
}

AngaraObject Angara_fs_is_dir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("is_dir(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);
    struct stat st;
    if (get_stat(path, &st, true) != 0) {
        return angara_create_bool(false);
    }
    // S_ISDIR checks if it's a directory.
    return angara_create_bool(S_ISDIR(st.st_mode));
}

AngaraObject Angara_fs_is_symlink(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("is_symlink(path) expects one string argument.");
        return angara_create_nil();
    }
    const char* path = AS_CSTRING(args[0]);
    struct stat st;
    // Use lstat (follow_symlinks = false) to check the link itself.
    if (get_stat(path, &st, false) != 0) {
        return angara_create_bool(false);
    }
    // S_ISLNK checks if it's a symbolic link.
    return angara_create_bool(S_ISLNK(st.st_mode));
}

static const AngaraFuncDef FS_EXPORTS[] = {
        {"read_file",       Angara_fs_read_file,       "s->s",    NULL},
        {"write_file",      Angara_fs_write_file,      "ss->n",   NULL},
        {"remove_file",     Angara_fs_remove_file,     "s->n",    NULL},
        {"create_dir",      Angara_fs_create_dir,      "s->n",    NULL},
        {"remove_dir",      Angara_fs_remove_dir,      "s->n",    NULL},
        {"rename_path",     Angara_fs_rename_path,     "ss->n",   NULL},
        {"create_symlink",  Angara_fs_create_symlink,  "ss->n",   NULL},
        {"create_hardlink", Angara_fs_create_hardlink, "ss->n",   NULL},
        {"exists",          Angara_fs_exists,          "s->b",    NULL},
        {"is_file",         Angara_fs_is_file,         "s->b",    NULL},
        {"is_dir",          Angara_fs_is_dir,          "s->b",    NULL},
        {"is_symlink",      Angara_fs_is_symlink,      "s->b",    NULL},
        {NULL, NULL, NULL, NULL}
};

ANGARA_MODULE_INIT(fs) {
    *def_count = (sizeof(FS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return FS_EXPORTS;
}
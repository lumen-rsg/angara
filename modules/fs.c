//
// fs.c — Angara filesystem module (rewritten for 16-byte ABI + vtable)
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "Angara.h"

static void throw_fs_error(const char* message, const char* path) {
    const char* error_reason = strerror(errno);
    char* full_message = (char*)malloc(strlen(message) + strlen(path) + strlen(error_reason) + 10);
    sprintf(full_message, "%s '%s': %s", message, path, error_reason);
    ang_api->throw_error(full_message);
    free(full_message);
}

// Helper: check if obj is a string
#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

AngaraObject Angara_fs_read_file(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("read_file(path) expects one string argument.");
        return ang_nil();
    }
    const char* path = ang_api->as_cstr(args[0]);

    FILE* file = fopen(path, "rb");
    if (!file) return ang_nil();

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) { fclose(file); ang_api->throw_error("Failed to allocate memory."); return ang_nil(); }

    fread(buffer, sizeof(char), file_size, file);
    buffer[file_size] = '\0';
    fclose(file);

    return ang_api->string_no_copy(buffer, file_size);
}

AngaraObject Angara_fs_write_file(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("write_file(path, content) expects two string arguments.");
        return ang_nil();
    }
    const char* path = ang_api->as_cstr(args[0]);
    const char* content = ang_api->as_cstr(args[1]);
    size_t content_len = ang_api->str_len(args[1]);

    FILE* file = fopen(path, "wb");
    if (!file) { throw_fs_error("Failed to open file for writing", path); return ang_nil(); }

    size_t written = fwrite(content, sizeof(char), content_len, file);
    fclose(file);

    if (written < content_len) { ang_api->throw_error("Failed to write entire content."); return ang_nil(); }
    return ang_nil();
}

AngaraObject Angara_fs_remove_file(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("remove_file(path) expects one string."); return ang_nil(); }
    if (remove(ang_api->as_cstr(args[0])) != 0) { throw_fs_error("Failed to remove file", ang_api->as_cstr(args[0])); }
    return ang_nil();
}

AngaraObject Angara_fs_create_dir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("create_dir(path) expects one string."); return ang_nil(); }
    if (mkdir(ang_api->as_cstr(args[0]), 0777) != 0) { throw_fs_error("Failed to create directory", ang_api->as_cstr(args[0])); }
    return ang_nil();
}

AngaraObject Angara_fs_remove_dir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("remove_dir(path) expects one string."); return ang_nil(); }
    if (rmdir(ang_api->as_cstr(args[0])) != 0) { throw_fs_error("Failed to remove directory", ang_api->as_cstr(args[0])); }
    return ang_nil();
}

AngaraObject Angara_fs_rename_path(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) { ang_api->throw_error("rename_path(old, new) expects two strings."); return ang_nil(); }
    if (rename(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1])) != 0) { throw_fs_error("Failed to rename", ang_api->as_cstr(args[0])); }
    return ang_nil();
}

AngaraObject Angara_fs_create_symlink(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) { ang_api->throw_error("create_symlink(target, link) expects two strings."); return ang_nil(); }
    if (symlink(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1])) != 0) { throw_fs_error("Failed to create symlink", ang_api->as_cstr(args[1])); }
    return ang_nil();
}

AngaraObject Angara_fs_create_hardlink(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) { ang_api->throw_error("create_hardlink(target, link) expects two strings."); return ang_nil(); }
    if (link(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1])) != 0) { throw_fs_error("Failed to create hard link", ang_api->as_cstr(args[1])); }
    return ang_nil();
}

AngaraObject Angara_fs_exists(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("exists(path) expects one string."); return ang_nil(); }
    struct stat st;
    return ang_bool(stat(ang_api->as_cstr(args[0]), &st) == 0);
}

AngaraObject Angara_fs_is_file(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_file(path) expects one string."); return ang_nil(); }
    struct stat st;
    if (stat(ang_api->as_cstr(args[0]), &st) != 0) return ang_bool(false);
    return ang_bool(S_ISREG(st.st_mode));
}

AngaraObject Angara_fs_is_dir(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_dir(path) expects one string."); return ang_nil(); }
    struct stat st;
    if (stat(ang_api->as_cstr(args[0]), &st) != 0) return ang_bool(false);
    return ang_bool(S_ISDIR(st.st_mode));
}

AngaraObject Angara_fs_is_symlink(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_symlink(path) expects one string."); return ang_nil(); }
    struct stat st;
    if (lstat(ang_api->as_cstr(args[0]), &st) != 0) return ang_bool(false);
    return ang_bool(S_ISLNK(st.st_mode));
}

AngaraObject Angara_fs_chmod(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("chmod(path, mode) expects (string, i64)."); return ang_nil();
    }
    if (chmod(ang_api->as_cstr(args[0]), (mode_t)ang_as_i64(args[1])) != 0) {
        char buf[256]; snprintf(buf, 256, "chmod failed for '%s': %s", ang_api->as_cstr(args[0]), strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

static int copy_file_contents(const char* source, const char* dest) {
    int src_fd = open(source, O_RDONLY);
    if (src_fd < 0) return -1;
    int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd < 0) { close(src_fd); return -1; }
    char buffer[4096]; ssize_t n;
    while ((n = read(src_fd, buffer, sizeof(buffer))) > 0) {
        if (write(dest_fd, buffer, n) != n) { close(src_fd); close(dest_fd); return -1; }
    }
    close(src_fd); close(dest_fd); return 0;
}

AngaraObject Angara_fs_install(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STR(args[0]) || !IS_STR(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("install(source, dest, mode) expects (string, string, i64)."); return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    const char* dst = ang_api->as_cstr(args[1]);
    mode_t mode = (mode_t)ang_as_i64(args[2]);
    if (copy_file_contents(src, dst) != 0) {
        char buf[256]; snprintf(buf, 256, "install: failed to copy '%s' to '%s': %s", src, dst, strerror(errno));
        ang_api->throw_error(buf); return ang_nil();
    }
    if (chmod(dst, mode) != 0) {
        char buf[256]; snprintf(buf, 256, "install: failed to chmod '%s': %s", dst, strerror(errno));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

AngaraObject Angara_fs_append_file(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("append_file(path, content) expects two strings."); return ang_nil();
    }
    const char* path = ang_api->as_cstr(args[0]);
    const char* content = ang_api->as_cstr(args[1]);
    size_t content_len = ang_api->str_len(args[1]);

    FILE* file = fopen(path, "a");
    if (!file) { char buf[256]; snprintf(buf, 256, "Failed to open '%s': %s", path, strerror(errno)); ang_api->throw_error(buf); return ang_nil(); }
    size_t written = fwrite(content, 1, content_len, file);
    fclose(file);
    if (written != content_len) { ang_api->throw_error("Incomplete write."); }
    return ang_nil();
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
    {"chmod",           Angara_fs_chmod,           "si->n",   NULL},
    {"install",         Angara_fs_install,         "ssi->n",  NULL},
    {"append_file",     Angara_fs_append_file,     "ss->n",   NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(fs) {
    ang_api = api;
    *def_count = (sizeof(FS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return FS_EXPORTS;
}
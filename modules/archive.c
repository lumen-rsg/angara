//
// archive.c — Angara archive/compression module
//
// Provides ZIP extraction/creation, gzip compress/decompress.
// Uses libarchive for ZIP/TAR and zlib for gzip.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v)(ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

// ---------------------------------------------------------------------------
// ZIP operations using miniz (embedded) or system libz
// We use a simple approach: shell out to system tools or use raw zlib.
// For maximum portability, we implement basic ZIP using the system's
// compression library.
//
// For simplicity and zero external deps beyond zlib (which is always present):
// ---------------------------------------------------------------------------

#include <zlib.h>

// --- archive.gzip_compress(data) -> string ---
AngaraObject Angara_archive_gzip_compress(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("gzip_compress(data) expects a string.");
        return ang_nil();
    }

    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t data_len = ang_api->str_len(args[0]);

    uLongf dest_len = compressBound((uLong)data_len);
    uint8_t* dest = (uint8_t*)malloc(dest_len);
    if (!dest) { ang_api->throw_error("gzip_compress: out of memory."); return ang_nil(); }

    int ret = compress(dest, &dest_len, data, (uLong)data_len);
    if (ret != Z_OK) {
        free(dest);
        ang_api->throw_error("gzip_compress: compression failed.");
        return ang_nil();
    }

    return ang_api->string_no_copy((char*)dest, (size_t)dest_len);
}

// --- archive.gzip_decompress(data, original_size?) -> string ---
AngaraObject Angara_archive_gzip_decompress(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("gzip_decompress(data, original_size?) expects a string.");
        return ang_nil();
    }

    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t data_len = ang_api->str_len(args[0]);

    // Try to decompress with increasing buffer sizes
    size_t dest_len = data_len * 4;
    if (arg_count >= 2 && ang_is_i64(args[1])) {
        dest_len = (size_t)ang_as_i64(args[1]);
    }
    if (dest_len < 256) dest_len = 256;

    for (int attempt = 0; attempt < 8; attempt++) {
        uint8_t* dest = (uint8_t*)malloc(dest_len);
        if (!dest) { ang_api->throw_error("gzip_decompress: out of memory."); return ang_nil(); }

        uLongf actual_len = (uLongf)dest_len;
        int ret = uncompress(dest, &actual_len, data, (uLong)data_len);
        if (ret == Z_OK) {
            return ang_api->string_no_copy((char*)dest, (size_t)actual_len);
        }
        free(dest);
        if (ret == Z_BUF_ERROR) {
            dest_len *= 2;
            continue;
        }
        ang_api->throw_error("gzip_decompress: decompression failed.");
        return ang_nil();
    }

    ang_api->throw_error("gzip_decompress: buffer too small after retries.");
    return ang_nil();
}

// --- archive.zlib_compress(data) -> string ---
AngaraObject Angara_archive_zlib_compress(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("zlib_compress(data) expects a string.");
        return ang_nil();
    }

    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t data_len = ang_api->str_len(args[0]);

    uLongf dest_len = compressBound((uLong)data_len);
    uint8_t* dest = (uint8_t*)malloc(dest_len + 1);
    if (!dest) { ang_api->throw_error("zlib_compress: out of memory."); return ang_nil(); }

    int ret = compress2(dest, &dest_len, data, (uLong)data_len, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        free(dest);
        ang_api->throw_error("zlib_compress: compression failed.");
        return ang_nil();
    }

    return ang_api->string_no_copy((char*)dest, (size_t)dest_len);
}

// --- archive.zlib_decompress(data, original_size?) -> string ---
AngaraObject Angara_archive_zlib_decompress(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("zlib_decompress(data, original_size?) expects a string.");
        return ang_nil();
    }

    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t data_len = ang_api->str_len(args[0]);

    size_t dest_len = data_len * 4;
    if (arg_count >= 2 && ang_is_i64(args[1])) {
        dest_len = (size_t)ang_as_i64(args[1]);
    }
    if (dest_len < 256) dest_len = 256;

    for (int attempt = 0; attempt < 8; attempt++) {
        uint8_t* dest = (uint8_t*)malloc(dest_len);
        if (!dest) { ang_api->throw_error("zlib_decompress: out of memory."); return ang_nil(); }

        uLongf actual_len = (uLongf)dest_len;
        int ret = uncompress(dest, &actual_len, data, (uLong)data_len);
        if (ret == Z_OK) {
            return ang_api->string_no_copy((char*)dest, (size_t)actual_len);
        }
        free(dest);
        if (ret == Z_BUF_ERROR) {
            dest_len *= 2;
            continue;
        }
        ang_api->throw_error("zlib_decompress: decompression failed.");
        return ang_nil();
    }

    ang_api->throw_error("zlib_decompress: buffer too small after retries.");
    return ang_nil();
}

// --- archive.crc32(data) -> i64 ---
AngaraObject Angara_archive_crc32(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("crc32(data) expects a string.");
        return ang_nil();
    }
    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    uLong crc = crc32(0L, data, (uInt)len);
    return ang_i64((int64_t)crc);
}

// --- archive.adler32(data) -> i64 ---
AngaraObject Angara_archive_adler32(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("adler32(data) expects a string.");
        return ang_nil();
    }
    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    uLong adler = adler32(0L, data, (uInt)len);
    return ang_i64((int64_t)adler);
}

// ---------------------------------------------------------------------------
// ZIP operations using libarchive
// ---------------------------------------------------------------------------

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <unistd.h>

// Helper: create directory recursively
static int mkdirs(const char* path) {
    char* tmp = strdup(path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    free(tmp);
    return 0;
}

// --- archive.zip_extract(path, dest_dir) -> nil ---
AngaraObject Angara_archive_zip_extract(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("zip_extract(path, dest_dir) expects two strings.");
        return ang_nil();
    }

    const char* filepath = ang_api->as_cstr(args[0]);
    const char* destdir = ang_api->as_cstr(args[1]);

    struct archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    int r = archive_read_open_filename(a, filepath, 65536);
    if (r != ARCHIVE_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "zip_extract: %s", archive_error_string(a));
        archive_read_free(a);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        char* fullpath = (char*)malloc(strlen(destdir) + strlen(pathname) + 2);
        snprintf(fullpath, strlen(destdir) + strlen(pathname) + 2, "%s/%s", destdir, pathname);
        archive_entry_set_pathname(entry, fullpath);

        // Create parent directories
        char* last_slash = strrchr(fullpath, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdirs(fullpath);
            *last_slash = '/';
        }

        r = archive_read_extract(a, entry, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
        free(fullpath);
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
            // Continue on errors (some entries may be unsupported)
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    return ang_nil();
}

// --- archive.zip_create(output_path, file_paths) -> nil ---
AngaraObject Angara_archive_zip_create(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_LIST(args[1])) {
        ang_api->throw_error("zip_create(output_path, file_paths) expects a string and a list.");
        return ang_nil();
    }

    const char* output_path = ang_api->as_cstr(args[0]);
    size_t nfiles = ang_api->list_len(args[1]);

    struct archive* a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_open_filename(a, output_path);

    for (size_t i = 0; i < nfiles; i++) {
        AngaraObject elem = ang_api->list_get(args[1], (int64_t)i);
        if (!IS_STR(elem)) { ang_api->decref(elem); continue; }

        const char* filepath = ang_api->as_cstr(elem);

        struct stat st;
        if (stat(filepath, &st) != 0) {
            ang_api->decref(elem);
            continue;
        }

        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, filepath);
        archive_entry_set_size(entry, st.st_size);
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_write_header(a, entry);

        FILE* fp = fopen(filepath, "rb");
        if (fp) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                archive_write_data(a, buf, n);
            }
            fclose(fp);
        }

        archive_entry_free(entry);
        ang_api->decref(elem);
    }

    archive_write_close(a);
    archive_write_free(a);
    return ang_nil();
}

// --- archive.tar_extract(path, dest_dir) -> nil ---
AngaraObject Angara_archive_tar_extract(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("tar_extract(path, dest_dir) expects two strings.");
        return ang_nil();
    }

    const char* filepath = ang_api->as_cstr(args[0]);
    const char* destdir = ang_api->as_cstr(args[1]);

    struct archive* a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_all(a);

    int r = archive_read_open_filename(a, filepath, 65536);
    if (r != ARCHIVE_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "tar_extract: %s", archive_error_string(a));
        archive_read_free(a);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        char* fullpath = (char*)malloc(strlen(destdir) + strlen(pathname) + 2);
        snprintf(fullpath, strlen(destdir) + strlen(pathname) + 2, "%s/%s", destdir, pathname);
        archive_entry_set_pathname(entry, fullpath);

        char* last_slash = strrchr(fullpath, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdirs(fullpath);
            *last_slash = '/';
        }

        r = archive_read_extract(a, entry, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
        free(fullpath);
    }

    archive_read_close(a);
    archive_read_free(a);
    return ang_nil();
}

// --- archive.tar_create(output_path, file_paths) -> nil ---
AngaraObject Angara_archive_tar_create(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_LIST(args[1])) {
        ang_api->throw_error("tar_create(output_path, file_paths) expects a string and a list.");
        return ang_nil();
    }

    const char* output_path = ang_api->as_cstr(args[0]);

    struct archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, output_path);

    size_t nfiles = ang_api->list_len(args[1]);
    for (size_t i = 0; i < nfiles; i++) {
        AngaraObject elem = ang_api->list_get(args[1], (int64_t)i);
        if (!IS_STR(elem)) { ang_api->decref(elem); continue; }

        const char* filepath = ang_api->as_cstr(elem);
        struct stat st;
        if (stat(filepath, &st) != 0) { ang_api->decref(elem); continue; }

        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, filepath);
        archive_entry_set_size(entry, st.st_size);
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_write_header(a, entry);

        FILE* fp = fopen(filepath, "rb");
        if (fp) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                archive_write_data(a, buf, n);
            }
            fclose(fp);
        }

        archive_entry_free(entry);
        ang_api->decref(elem);
    }

    archive_write_close(a);
    archive_write_free(a);
    return ang_nil();
}

// --- Export Table ---

static const AngaraFuncDef ARCHIVE_EXPORTS[] = {
    {"gzip_compress",   Angara_archive_gzip_compress,   "s->s",    NULL},
    {"gzip_decompress", Angara_archive_gzip_decompress,  "si?->s",  NULL},
    {"zlib_compress",   Angara_archive_zlib_compress,    "s->s",    NULL},
    {"zlib_decompress", Angara_archive_zlib_decompress,   "si?->s",  NULL},
    {"crc32",           Angara_archive_crc32,             "s->i",    NULL},
    {"adler32",         Angara_archive_adler32,           "s->i",    NULL},
    {"zip_extract",     Angara_archive_zip_extract,       "ss->n",   NULL},
    {"zip_create",      Angara_archive_zip_create,        "sl<s>->n",NULL},
    {"tar_extract",     Angara_archive_tar_extract,       "ss->n",   NULL},
    {"tar_create",      Angara_archive_tar_create,        "sl<s>->n",NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(archive) {
    ang_api = api;
    *def_count = (sizeof(ARCHIVE_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ARCHIVE_EXPORTS;
}
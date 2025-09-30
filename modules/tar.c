//
// Created by cv2 on 9/29/25.
//
#include "../runtime/angara_runtime.h"
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// --- Helper Functions ---

// Helper to copy data from a source fd to the archive writer
static void write_file_to_archive(struct archive *a, const char *filepath) {
    struct archive_entry *entry;
    struct stat st;
    char buff[8192];
    int len;
    int fd;

    stat(filepath, &st);
    entry = archive_entry_new();
    archive_entry_set_pathname(entry, filepath);
    archive_entry_set_size(entry, st.st_size);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);

    fd = open(filepath, O_RDONLY);
    len = read(fd, buff, sizeof(buff));
    while (len > 0) {
        archive_write_data(a, buff, len);
        len = read(fd, buff, sizeof(buff));
    }
    close(fd);
    archive_entry_free(entry);
}

// Helper to copy data from the archive reader to a new file on disk
static int copy_data(struct archive *ar, struct archive *aw) {
    int r;
    const void *buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) return (ARCHIVE_OK);
        if (r < ARCHIVE_OK) return (r);
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK) {
            fprintf(stderr, "%s\n", archive_error_string(aw));
            return (r);
        }
    }
}

// --- Angara-Exported Functions ---

// Angara signature: func pack(archive_name as string, files as list<string>) -> nil
AngaraObject Angara_tar_pack(int arg_count, AngaraObject args[]) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_LIST(args[1])) {
        angara_throw_error("archive.pack() requires 2 arguments: (string, list<string>).");
        return angara_create_nil();
    }

    const char* archive_name = AS_CSTRING(args[0]);
    AngaraList* file_list = AS_LIST(args[1]);

    struct archive *a;
    a = archive_write_new();
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, archive_name);

    for (size_t i = 0; i < file_list->count; ++i) {
        AngaraObject file_obj = file_list->elements[i];
        if (IS_STRING(file_obj)) {
            write_file_to_archive(a, AS_CSTRING(file_obj));
        }
    }

    archive_write_close(a);
    archive_write_free(a);
    return angara_create_nil();
}

// Angara signature: func unpack(archive_name as string, dest_dir as string) -> nil
AngaraObject Angara_tar_unpack(int arg_count, AngaraObject args[]) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        angara_throw_error("archive.unpack() requires 2 arguments: (string, string).");
        return angara_create_nil();
    }

    const char* archive_name = AS_CSTRING(args[0]);
    const char* dest_dir = AS_CSTRING(args[1]);

    // --- 1. SETUP: Declare all variables and initialize resources ---
    struct archive *a = archive_read_new();
    struct archive *ext = archive_write_disk_new();
    struct archive_entry *entry;
    int r = ARCHIVE_OK;
    int exit_code = ARCHIVE_OK; // Use a variable to track the final status

    if (!a || !ext) {
        angara_throw_error("Out of memory: could not create archive handles.");
        exit_code = ARCHIVE_FATAL;
        goto cleanup; // Use goto only for the final cleanup block
    }

    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS;
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    // --- 2. EXECUTION: Perform the core logic ---
    r = archive_read_open_filename(a, archive_name, 10240);
    if (r != ARCHIVE_OK) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "Failed to open archive '%s': %s", archive_name, archive_error_string(a));
        angara_throw_error(err_buf);
        exit_code = r;
    } else {
        // Only proceed if the archive was opened successfully.
        while ( (r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
            const char* current_file = archive_entry_pathname(entry);
            char* dest_path = malloc(strlen(dest_dir) + strlen(current_file) + 2);
            if (!dest_path) {
                angara_throw_error("Out of memory building destination path.");
                exit_code = ARCHIVE_FATAL;
                break; // Exit the loop on critical failure
            }

            strcpy(dest_path, dest_dir);
            strcat(dest_path, "/");
            strcat(dest_path, current_file);
            archive_entry_set_pathname(entry, dest_path);
            free(dest_path);

            r = archive_write_header(ext, entry);
            if (r != ARCHIVE_OK) {
                angara_throw_error(archive_error_string(ext));
                exit_code = r;
                break;
            } else if (archive_entry_size(entry) > 0) {
                r = copy_data(a, ext);
                if (r != ARCHIVE_OK) {
                    angara_throw_error(archive_error_string(ext));
                    exit_code = r;
                    break;
                }
            }
            r = archive_write_finish_entry(ext);
            if (r != ARCHIVE_OK) {
                angara_throw_error(archive_error_string(ext));
                exit_code = r;
                break;
            }
        }
        // Check if the loop terminated because of an error or EOF
        if (r != ARCHIVE_EOF) {
            if (exit_code == ARCHIVE_OK) { // Only report if not already set
                angara_throw_error(archive_error_string(a));
            }
        }
    }

// --- 3. CLEANUP: A single, guaranteed cleanup point at the end of the function ---
    cleanup:
    if (a) {
        archive_read_close(a);
        archive_read_free(a);
    }
    if (ext) {
        archive_write_close(ext);
        archive_write_free(ext);
    }

    // The Angara function itself doesn't signal error via return value,
    // it uses exceptions. The return is always nil.
    return angara_create_nil();
}

AngaraObject Angara_tar_add(int arg_count, AngaraObject args[]) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_LIST(args[1])) {
        angara_throw_error("archive.add() requires 2 arguments: (string, list<string>).");
        return angara_create_nil();
    }

    const char* archive_name = AS_CSTRING(args[0]);
    AngaraList* files_to_add = AS_LIST(args[1]);

    // 1. Create a temporary name for the new archive.
    char temp_archive_name[256];
    snprintf(temp_archive_name, sizeof(temp_archive_name), "%s.tmp", archive_name);

    struct archive *in_archive;
    struct archive *out_archive;
    struct archive_entry *entry;
    int r;

    in_archive = archive_read_new();
    out_archive = archive_write_new();

    // Configure the output archive (same as `pack`)
    archive_write_add_filter_gzip(out_archive);
    archive_write_set_format_pax_restricted(out_archive);
    archive_write_open_filename(out_archive, temp_archive_name);

    // Configure the input archive (same as `unpack`)
    archive_read_support_format_all(in_archive);
    archive_read_support_filter_all(in_archive);

    // 2. Open the existing source archive for reading.
    if ((r = archive_read_open_filename(in_archive, archive_name, 10240))) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "Failed to open archive '%s' for reading.", archive_name);
        angara_throw_error(err_buf);
        goto cleanup;
    }

    // 3. Copy all existing entries from the old archive to the new one.
    while ((r = archive_read_next_header(in_archive, &entry)) == ARCHIVE_OK) {
        archive_write_header(out_archive, entry);
        if (archive_entry_size(entry) > 0) {
            copy_data(in_archive, out_archive);
        }
    }
    // Check for a read error
    if (r != ARCHIVE_EOF) {
        angara_throw_error(archive_error_string(in_archive));
        goto cleanup;
    }

    // 4. Now, add all the new files to the output archive.
    for (size_t i = 0; i < files_to_add->count; ++i) {
        AngaraObject file_obj = files_to_add->elements[i];
        if (IS_STRING(file_obj)) {
            write_file_to_archive(out_archive, AS_CSTRING(file_obj));
        }
    }

    // 5. Finalize the archives and replace the original.
    cleanup:
    archive_read_close(in_archive);
    archive_read_free(in_archive);
    archive_write_close(out_archive);
    archive_write_free(out_archive);

    // If we succeeded, replace the original file with the temporary one.
    if (r == ARCHIVE_EOF) {
        rename(temp_archive_name, archive_name);
    } else {
        // If we failed, remove the temporary file.
        remove(temp_archive_name);
    }

    return angara_create_nil();
}

static int should_remove_file(const char* filename, AngaraList* files_to_remove) {
    for (size_t i = 0; i < files_to_remove->count; ++i) {
        AngaraObject file_obj = files_to_remove->elements[i];
        if (IS_STRING(file_obj)) {
            if (strcmp(filename, AS_CSTRING(file_obj)) == 0) {
                return 1; // Found it, should be removed.
            }
        }
    }
    return 0; // Not found, should be kept.
}

AngaraObject Angara_tar_remove(int arg_count, AngaraObject args[]) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_LIST(args[1])) {
        angara_throw_error("archive.remove() requires 2 arguments: (string, list<string>).");
        return angara_create_nil();
    }

    const char* archive_name = AS_CSTRING(args[0]);
    AngaraList* files_to_remove = AS_LIST(args[1]);

    char temp_archive_name[256];
    snprintf(temp_archive_name, sizeof(temp_archive_name), "%s.tmp", archive_name);

    struct archive *in_archive;
    struct archive *out_archive;
    struct archive_entry *entry;
    int r;

    in_archive = archive_read_new();
    out_archive = archive_write_new();

    archive_write_add_filter_gzip(out_archive);
    archive_write_set_format_pax_restricted(out_archive);
    archive_write_open_filename(out_archive, temp_archive_name);

    archive_read_support_format_all(in_archive);
    archive_read_support_filter_all(in_archive);

    if ((r = archive_read_open_filename(in_archive, archive_name, 10240))) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "Failed to open archive '%s' for reading.", archive_name);
        angara_throw_error(err_buf);
        goto cleanup;
    }

    // 3. Iterate, filter, and rewrite the archive.
    while ((r = archive_read_next_header(in_archive, &entry)) == ARCHIVE_OK) {
        // --- THIS IS THE CORE REMOVAL LOGIC ---
        if (should_remove_file(archive_entry_pathname(entry), files_to_remove)) {
            // If the file is in our removal list, simply skip to the next header.
            continue;
        }
        // --- END OF LOGIC ---

        // If we are here, the file should be kept. Copy it to the new archive.
        archive_write_header(out_archive, entry);
        if (archive_entry_size(entry) > 0) {
            copy_data(in_archive, out_archive);
        }
    }

    if (r != ARCHIVE_EOF) {
        angara_throw_error(archive_error_string(in_archive));
    }

    cleanup:
    archive_read_close(in_archive);
    archive_read_free(in_archive);
    archive_write_close(out_archive);
    archive_write_free(out_archive);

    if (r == ARCHIVE_EOF) {
        rename(temp_archive_name, archive_name);
    } else {
        remove(temp_archive_name);
    }

    return angara_create_nil();
}


// --- ABI Definition Table ---
AngaraFuncDef TAR_EXPORTS[] = {
        {"pack",   Angara_tar_pack,   "sl<s>->n", NULL},
        {"unpack", Angara_tar_unpack, "ss->n",   NULL},
        {"add",    Angara_tar_add,    "sl<s>->n", NULL},
        {"remove", Angara_tar_remove, "sl<s>->n", NULL},
        {NULL, NULL, NULL, NULL}
};

// --- Module Entry Point ---
const AngaraFuncDef* Angara_tar_Init(int* def_count) {
    *def_count = (sizeof(TAR_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return TAR_EXPORTS;
}
#include "../runtime/angara_runtime.h" // The provided Angara ABI header

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUNDLE_BASE_PATH "/run/containers"
#define PATH_BUFFER_SIZE 1024
#define MAX_UMOUNT_RETRIES 10
#define UMOUNT_RETRY_DELAY_US 100000 // 100 milliseconds

// --- Forward Declarations of Helper Functions ---

static int recursive_mkdir(const char* path, mode_t mode);
static bool generate_runc_config(const char* config_path, const char* rootfs_path, AngaraObject command_list_obj);
static int execute_runc(const char* bundle_path, const char* container_id);
static void cleanup_container(const char* bundle_path, const char* merged_path);

// --- The Main Native Function Exposed to Angara ---
AngaraObject Angara_container_run(int arg_count, AngaraObject args[]) {
    // ... (This entire function remains unchanged) ...
    if (arg_count != 1 || !IS_RECORD(args[0])) {
        angara_throw_error("container.run() requires one record argument (the manifest).");
    }
    AngaraObject manifest = args[0];

    AngaraObject id_obj = angara_record_get(manifest, "id");
    AngaraObject runtime_path_obj = angara_record_get(manifest, "runtime_path");
    AngaraObject app_path_obj = angara_record_get(manifest, "app_path");
    AngaraObject command_obj = angara_record_get(manifest, "command");

    if (!IS_STRING(id_obj) || !IS_STRING(runtime_path_obj) || !IS_STRING(app_path_obj) || !IS_LIST(command_obj)) {
        angara_throw_error("Manifest record is missing required fields: id, runtime_path, app_path (strings) and command (list).");
    }

    const char* container_id = AS_CSTRING(id_obj);
    const char* runtime_path = AS_CSTRING(runtime_path_obj);
    const char* app_path = AS_CSTRING(app_path_obj);

    char bundle_path[PATH_BUFFER_SIZE];
    char merged_path[PATH_BUFFER_SIZE];
    char upper_path[PATH_BUFFER_SIZE];
    char work_path[PATH_BUFFER_SIZE];
    char config_path[PATH_BUFFER_SIZE];

    snprintf(bundle_path, sizeof(bundle_path), "%s/%s", BUNDLE_BASE_PATH, container_id);
    snprintf(merged_path, sizeof(merged_path), "%s/merged", bundle_path);
    snprintf(upper_path, sizeof(upper_path), "%s/upper", bundle_path);
    snprintf(work_path, sizeof(work_path), "%s/work", bundle_path);
    snprintf(config_path, sizeof(config_path), "%s/config.json", bundle_path);

    bool is_mounted = false;
    int exit_code = -1;

    if (recursive_mkdir(work_path, 0755) != 0 ||
        recursive_mkdir(upper_path, 0755) != 0 ||
        recursive_mkdir(merged_path, 0755) != 0) {
        perror("Failed to create container bundle directories");
        goto cleanup;
    }

    if (!generate_runc_config(config_path, merged_path, command_obj)) {
        fprintf(stderr, "Failed to generate runc config.json\n");
        goto cleanup;
    }

    char mount_opts[PATH_BUFFER_SIZE * 3];
    snprintf(mount_opts, sizeof(mount_opts), "lowerdir=%s:%s,upperdir=%s,workdir=%s",
             app_path, runtime_path, upper_path, work_path);

    if (mount("overlay", merged_path, "overlay", 0, mount_opts) != 0) {
        perror("Failed to mount overlayfs");
        goto cleanup;
    }
    is_mounted = true;

    printf("Successfully mounted overlayfs. Executing runc...\n");
    exit_code = execute_runc(bundle_path, container_id);

    cleanup:
    if (is_mounted) {
        if (umount(merged_path) != 0) {
            perror("WARNING: Failed to unmount overlayfs. Manual cleanup may be required");
        }
    }
    cleanup_container(bundle_path, merged_path);

    return angara_create_i64(exit_code);
}


// --- Helper Function Implementations ---

//
//  THE FIX IS IN THIS FUNCTION
//
static int execute_runc(const char* bundle_path, const char* container_id) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }

    if (pid == 0) { // Child process
        // **THE FIX:** Use an absolute path to 'runc' and switch to execv.
        // Common locations are /usr/bin/runc or /usr/sbin/runc.
        const char* runc_path = "/usr/bin/runc";

        char* const argv[] = {"runc", "run", "--bundle", (char*)bundle_path, (char*)container_id, NULL};

        // Use execv, which takes an absolute path and does NOT search PATH.
        execv(runc_path, argv);

        // If we get here, it means execv failed.
        perror("execv(/usr/bin/runc) failed");
        _exit(127);
    } else { // Parent process
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return -1; // Indicate non-normal termination
    }
}

// ... (The rest of the helper functions are unchanged) ...
static bool generate_runc_config(const char* config_path, const char* rootfs_path, AngaraObject command_list_obj) {
    FILE* fp = fopen(config_path, "w");
    if (!fp) {
        perror("Failed to open config.json for writing");
        return false;
    }

    AngaraList* command_list = AS_LIST(command_list_obj);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"ociVersion\": \"1.0.2-dev\",\n");
    fprintf(fp, "  \"process\": {\n");
    fprintf(fp, "    \"args\": [\n");

    for (size_t i = 0; i < command_list->count; i++) {
        AngaraObject arg_obj = command_list->elements[i];
        if (!IS_STRING(arg_obj)) {
            fprintf(stderr, "Command list contains a non-string element.\n");
            fclose(fp);
            return false;
        }
        fprintf(fp, "      \"%s\"%s\n", AS_CSTRING(arg_obj), (i == command_list->count - 1) ? "" : ",");
    }

    fprintf(fp, "    ],\n");
    // ******** THIS IS THE FIX ********
    fprintf(fp, "    \"cwd\": \"/\"\n");
    // *******************************
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"root\": {\n");
    fprintf(fp, "    \"path\": \"%s\",\n", rootfs_path);
    fprintf(fp, "    \"readonly\": false\n");
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"mounts\": [\n");
    fprintf(fp, "    {\n");
    fprintf(fp, "      \"destination\": \"/proc\",\n");
    fprintf(fp, "      \"type\": \"proc\",\n");
    fprintf(fp, "      \"source\": \"proc\"\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return true;
}

static void cleanup_container(const char* bundle_path, const char* merged_path) {
    // --- Robust Unmount with Retry Loop ---
    bool unmounted_successfully = false;
    for (int i = 0; i < MAX_UMOUNT_RETRIES; i++) {
        if (umount(merged_path) == 0) {
            unmounted_successfully = true;
            break; // Success!
        }

        // If the error is NOT EBUSY, it's a real problem. Stop trying.
        if (errno != EBUSY) {
            perror("Fatal error during umount");
            break;
        }

        // If it was EBUSY, wait a bit and try again.
        usleep(UMOUNT_RETRY_DELAY_US);
    }

    if (!unmounted_successfully) {
        fprintf(stderr, "WARNING: Failed to unmount overlayfs after %d retries. Manual cleanup may be required.\n", MAX_UMOUNT_RETRIES);
    }

    // --- Clean up directories ---
    char path_buffer[PATH_BUFFER_SIZE];
    snprintf(path_buffer, sizeof(path_buffer), "%s/merged", bundle_path); rmdir(path_buffer);
    snprintf(path_buffer, sizeof(path_buffer), "%s/upper", bundle_path); rmdir(path_buffer);
    snprintf(path_buffer, sizeof(path_buffer), "%s/work", bundle_path); rmdir(path_buffer);
    snprintf(path_buffer, sizeof(path_buffer), "%s/config.json", bundle_path); unlink(path_buffer);
    rmdir(bundle_path);
}


static int recursive_mkdir(const char* path, mode_t mode) {
    char* path_copy = strdup(path);
    if (!path_copy) return -1;

    char* p = path_copy;
    while (*p == '/') p++;

    int result = 0;
    while ((p = strchr(p, '/'))) {
        *p = '\0';
        if (mkdir(path_copy, mode) != 0 && errno != EEXIST) {
            result = -1;
            break;
        }
        *p = '/';
        p++;
    }

    if (result == 0) {
        if (mkdir(path_copy, mode) != 0 && errno != EEXIST) {
            result = -1;
        }
    }

    free(path_copy);
    return result;
}

// --- Angara ABI Definition Block ---
static const AngaraFuncDef container_functions[] = {
        { "run", Angara_container_run, "{}->i", NULL },
        { NULL, NULL, NULL, NULL }
};

const AngaraFuncDef* Angara_container_Init(int* def_count) {
    *def_count = 1;
    return container_functions;
}
// =======================================================================
// Create a new file: http.c
// =======================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <ctype.h> // For isspace
#include "../src/runtime/angara_runtime.h"

// --- libcurl callback helpers ---

// A struct to hold the state for a single request, primarily for the response body.
typedef struct {
    char *buffer;
    size_t len;
    size_t cap;
} MemoryBuffer;

// This is the callback function that libcurl will call for every chunk of data it receives.
// Our job is to append this new data to our MemoryBuffer.
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    MemoryBuffer *mem = (MemoryBuffer *)userp;

    // Ensure there is enough space for the new data AND a null terminator.
    while (mem->len + real_size + 1 > mem->cap) {
        // If cap is 0, start with a reasonable size.
        mem->cap = mem->cap ? mem->cap * 2 : 4096;
        char *new_buffer = (char *)realloc(mem->buffer, mem->cap);
        if (!new_buffer) {
            if (mem->buffer) free(mem->buffer);
            return 0; // Out of memory
        }
        mem->buffer = new_buffer;
    }

    memcpy(&(mem->buffer[mem->len]), contents, real_size);
    mem->len += real_size;
    // We do NOT add the null terminator here, but we've ensured space for it.

    return real_size;
}


// --- Main Module Function ---

AngaraObject Angara_http_request(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_RECORD(args[0])) { // Macro needed!
        angara_throw_error("request(options) expects one record argument.");
        return angara_create_nil();
    }
    AngaraObject options = args[0];

    // --- Extract options from the record ---
    AngaraObject url_obj = angara_record_get(options, "url");
    if (!IS_STRING(url_obj)) {
        angara_throw_error("request options must include a 'url' string field.");
        return angara_create_nil();
    }
    const char* url = AS_CSTRING(url_obj);

    AngaraObject method_obj = angara_record_get(options, "method");
    if (!IS_STRING(method_obj)) {
        angara_throw_error("request options must include a 'method' string field.");
        return angara_create_nil();
    }
    const char* method = AS_CSTRING(method_obj);

    // Optional: Request body
    AngaraObject body_obj = angara_record_get(options, "body");
    const char* request_body_data = NULL;
    if (IS_STRING(body_obj)) {
        request_body_data = AS_CSTRING(body_obj);
    }

    // Optional: Request headers
    AngaraObject headers_obj = angara_record_get(options, "headers");

    // --- Initialize libcurl ---
    CURL *curl_handle = curl_easy_init();
    if (!curl_handle) {
        angara_throw_error("Failed to initialize libcurl.");
        return angara_create_nil();
    }

    struct curl_slist *header_list = NULL; // For request headers

    // --- Set basic options ---
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects

    // --- NEW: Handle Request Body ---
    if (request_body_data) {
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, request_body_data);
    }

    // --- NEW: Handle Request Headers ---
    if (IS_RECORD(headers_obj)) {
        AngaraRecord* headers_record = AS_RECORD(headers_obj);
        for (size_t i = 0; i < headers_record->count; i++) {
            const char* key = headers_record->entries[i].key;
            AngaraObject val_obj = headers_record->entries[i].value;
            if (IS_STRING(val_obj)) {
                const char* value = AS_CSTRING(val_obj);
                char header_string[1024];
                snprintf(header_string, sizeof(header_string), "%s: %s", key, value);
                header_list = curl_slist_append(header_list, header_string);
            }
        }
        if (header_list) {
            curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, header_list);
        }
    }

    // --- Set up response body handling ---
    MemoryBuffer body_buffer = { .buffer = NULL, .len = 0, .cap = 0 };
    body_buffer.cap = 4096; // Start with 4KB
    body_buffer.buffer = (char *)malloc(body_buffer.cap);
    body_buffer.len = 0;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&body_buffer);

    // --- NEW: Set up response header handling ---
    MemoryBuffer header_buffer = { .buffer = NULL, .len = 0, .cap = 0 };
    header_buffer.cap = 2048;
    header_buffer.buffer = (char *)malloc(header_buffer.cap);
    header_buffer.len = 0;
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&header_buffer);

    // --- Perform the request ---
    CURLcode res = curl_easy_perform(curl_handle);

    // --- Free request-related memory ASAP ---
    if (header_list) {
        curl_slist_free_all(header_list);
    }

    angara_decref(url_obj); // Release refs to objects we extracted
    angara_decref(method_obj);
    angara_decref(body_obj);
    angara_decref(headers_obj);

    // --- Check for errors ---
    if (res != CURLE_OK) {
        const char* curl_err = curl_easy_strerror(res);
        angara_throw_error(curl_err);
        curl_easy_cleanup(curl_handle);
        free(body_buffer.buffer);
        return angara_create_nil();
    }

    long status_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &status_code);

    // --- Construct the Angara response record ---
    AngaraObject response_record = angara_record_new();
    angara_record_set(response_record, "status_code", angara_create_i64(status_code));

    body_buffer.buffer[body_buffer.len] = '\0';
    angara_record_set(response_record, "body", angara_create_string_no_copy(body_buffer.buffer, body_buffer.len));

    // --- NEW: Parse response headers and add them to the record ---
    AngaraObject response_headers_record = angara_record_new();
    header_buffer.buffer[header_buffer.len] = '\0'; // Null-terminate the whole buffer

    char* line = strtok(header_buffer.buffer, "\r\n");
    while (line != NULL) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0'; // Split the line into key and value
            char* value = colon + 1;
            // Trim leading whitespace from value
            while (isspace(*value)) {
                value++;
            }
            // Keys and values are now null-terminated strings
            angara_record_set(response_headers_record, line, angara_string_from_c(value));
        }
        line = strtok(NULL, "\r\n");
    }
    angara_record_set(response_record, "headers", response_headers_record);

    // --- Cleanup ---
    curl_easy_cleanup(curl_handle);
    free(header_buffer.buffer); // Free the header buffer

    return response_record;
}


// --- Module Definition ---

static const AngaraFuncDef HTTP_EXPORTS[] = {
        {"request", Angara_http_request, "{}->a", NULL},
        {NULL, NULL, NULL, NULL}
};

ANGARA_MODULE_INIT(http) {
    curl_global_init(CURL_GLOBAL_ALL);
    *def_count = (sizeof(HTTP_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return HTTP_EXPORTS;
}
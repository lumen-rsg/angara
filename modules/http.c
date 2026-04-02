#include "../runtime/angara_runtime.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

// --- libcurl Data Structures ---

// A struct to hold the response data from a curl request.
typedef struct {
    char *buffer;
    size_t size;
} MemoryStruct;

// A struct to hold all the data we need to pass to libcurl.
typedef struct {
    const char *body;
    size_t size;
    size_t sent;
} RequestBody;

// --- libcurl Callback Functions ---

// This callback is fired by curl whenever it receives data.
// It appends the new data chunk to our MemoryStruct buffer.
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;

    char *ptr = realloc(mem->buffer, mem->size + realsize + 1);
    if (ptr == NULL) {
        // Out of memory!
        return 0;
    }

    mem->buffer = ptr;
    memcpy(&(mem->buffer[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->buffer[mem->size] = 0;

    return realsize;
}

// This callback is fired by curl when it needs to send POST data.
// It reads from our RequestBody struct.
static size_t read_callback(char *buffer, size_t size, size_t nitems, void *userp) {
    RequestBody *body = (RequestBody *)userp;
    size_t buffer_size = size * nitems;

    if (body->sent >= body->size) {
        return 0; // Nothing left to send
    }

    size_t to_send = body->size - body->sent;
    if (to_send > buffer_size) {
        to_send = buffer_size;
    }

    memcpy(buffer, body->body + body->sent, to_send);
    body->sent += to_send;

    return to_send;
}


// --- Angara-Exported Function: http.request ---
// Angara signature: func request(options as record) -> record
AngaraObject Angara_http_request(int arg_count, AngaraObject args[]) {
    CURLcode res = CURLE_OK;
    struct curl_slist *headers = NULL;
    if (arg_count != 1 || !IS_RECORD(args[0])) {
        angara_throw_error("http.request() requires one record argument for options.");
        return angara_create_nil();
    }
    AngaraObject options = args[0];

    // --- 1. Initialize Curl and Response Struct ---
    CURL *curl_handle = curl_easy_init();
    if (!curl_handle) {
        angara_throw_error("Failed to initialize libcurl.");
        return angara_create_nil();
    }
    MemoryStruct chunk = { .buffer = malloc(1), .size = 0 };
    if (!chunk.buffer) {
        curl_easy_cleanup(curl_handle);
        angara_throw_error("Out of memory for HTTP response.");
        return angara_create_nil();
    }
    chunk.buffer[0] = '\0';


    // --- 2. Extract Options from the Angara Record ---
    AngaraObject url_obj = angara_record_get(options, "url");
    AngaraObject method_obj = angara_record_get(options, "method");
    AngaraObject headers_obj = angara_record_get(options, "headers");
    AngaraObject body_obj = angara_record_get(options, "body");

    if (!IS_STRING(url_obj)) {
        angara_throw_error("http.request options must include a 'url' of type string.");
        goto cleanup;
    }
    const char* url = AS_CSTRING(url_obj);

    const char* method = "GET";
    if (IS_STRING(method_obj)) {
        method = AS_CSTRING(method_obj);
    }

    RequestBody req_body = { .body = NULL, .size = 0, .sent = 0 };
    if (IS_STRING(body_obj)) {
        req_body.body = AS_CSTRING(body_obj);
        req_body.size = AS_STRING(body_obj)->length;
    }

    // --- 3. Set libcurl Options ---
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "angara-http-client/1.0");

    // Set method and body for POST/PUT etc.
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION, read_callback);
        curl_easy_setopt(curl_handle, CURLOPT_READDATA, &req_body);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req_body.size);
    } // Add other methods like PUT, DELETE as needed

    // Set custom headers
    if (IS_RECORD(headers_obj)) {
        AngaraRecord* headers_rec = AS_RECORD(headers_obj);
        for (size_t i = 0; i < headers_rec->count; ++i) {
            char header_string[1024];
            snprintf(header_string, sizeof(header_string), "%s: %s",
                     headers_rec->entries[i].key,
                     AS_CSTRING(headers_rec->entries[i].value));
            headers = curl_slist_append(headers, header_string);
        }
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    }

    // --- 4. Perform the Request ---
    res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "http.request failed: %s", curl_easy_strerror(res));
        angara_throw_error(err_buf);
        goto cleanup;
    }

    // --- 5. Get Response Info and Create Result Record ---
    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    AngaraObject body_str = angara_create_string_no_copy(chunk.buffer, chunk.size);
    AngaraObject status_code = angara_create_i64(http_code);

    AngaraObject result_record = angara_record_new();
    angara_record_set(result_record, "status", status_code);
    angara_record_set(result_record, "body", body_str);

    // --- 6. Cleanup ---
cleanup:
    curl_easy_cleanup(curl_handle);
    if (headers) curl_slist_free_all(headers);
    angara_decref(url_obj);
    angara_decref(method_obj);
    angara_decref(headers_obj);
    angara_decref(body_obj);

    // The result record now owns the body string, no need to free chunk.buffer
    // or decref body_str if it was successfully set.
    if (res != CURLE_OK) {
        free(chunk.buffer);
    }

    return (res == CURLE_OK) ? result_record : angara_create_nil();
}


// --- ABI Definition Table ---
static const AngaraFuncDef HTTP_EXPORTS[] = {
    {"request", Angara_http_request, "{}->{}", NULL},
    {NULL, NULL, NULL, NULL}
};

// --- Module Entry Point ---
const AngaraFuncDef* Angara_http_Init(int* def_count) {
    *def_count = 1;
    return HTTP_EXPORTS;
}
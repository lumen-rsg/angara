//
// http.c — Angara HTTP client module (rewritten for 16-byte ABI + vtable)
//

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

typedef struct { char* buffer; size_t size; } MemoryStruct;
typedef struct { const char* body; size_t size; size_t sent; } RequestBody;

static size_t write_memory_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    MemoryStruct* mem = (MemoryStruct*)userp;
    char* ptr = realloc(mem->buffer, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->buffer = ptr;
    memcpy(&(mem->buffer[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->buffer[mem->size] = 0;
    return realsize;
}

static size_t read_callback(char* buffer, size_t size, size_t nitems, void* userp) {
    RequestBody* body = (RequestBody*)userp;
    size_t buf_size = size * nitems;
    if (body->sent >= body->size) return 0;
    size_t to_send = body->size - body->sent;
    if (to_send > buf_size) to_send = buf_size;
    memcpy(buffer, body->body + body->sent, to_send);
    body->sent += to_send;
    return to_send;
}

// http.request(options: record) -> record{status: i64, body: string}
AngaraObject Angara_http_request(int arg_count, AngaraObject args[]) {
    CURLcode res = CURLE_OK;
    struct curl_slist* headers = NULL;

    if (arg_count != 1 || !IS_REC(args[0])) {
        ang_api->throw_error("http.request() requires one record argument.");
        return ang_nil();
    }
    AngaraObject options = args[0];

    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) { ang_api->throw_error("Failed to initialize libcurl."); return ang_nil(); }

    MemoryStruct chunk = { .buffer = malloc(1), .size = 0 };
    if (!chunk.buffer) { curl_easy_cleanup(curl_handle); ang_api->throw_error("OOM."); return ang_nil(); }
    chunk.buffer[0] = '\0';

    // Extract options
    AngaraObject url_obj = ang_api->record_get(options, "url");
    AngaraObject method_obj = ang_api->record_get(options, "method");
    AngaraObject headers_obj = ang_api->record_get(options, "headers");
    AngaraObject body_obj = ang_api->record_get(options, "body");

    if (!IS_STR(url_obj)) {
        ang_api->throw_error("http.request options must include 'url' (string).");
        goto cleanup;
    }
    const char* url = ang_api->as_cstr(url_obj);
    const char* method = IS_STR(method_obj) ? ang_api->as_cstr(method_obj) : "GET";

    RequestBody req_body = { .body = NULL, .size = 0, .sent = 0 };
    if (IS_STR(body_obj)) {
        req_body.body = ang_api->as_cstr(body_obj);
        req_body.size = ang_api->str_len(body_obj);
    }

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "angara-http-client/1.0");

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION, read_callback);
        curl_easy_setopt(curl_handle, CURLOPT_READDATA, &req_body);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req_body.size);
    }

    if (IS_REC(headers_obj)) {
        size_t hcount = ang_api->record_len(headers_obj);
        for (size_t i = 0; i < hcount; ++i) {
            const char* key = ang_api->record_key_at(headers_obj, i);
            AngaraObject val = ang_api->record_val_at(headers_obj, i);
            char header_string[1024];
            snprintf(header_string, sizeof(header_string), "%s: %s", key, IS_STR(val) ? ang_api->as_cstr(val) : "");
            headers = curl_slist_append(headers, header_string);
            ang_api->decref(val);
        }
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    }

    res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        char buf[256]; snprintf(buf, 256, "http.request failed: %s", curl_easy_strerror(res));
        ang_api->throw_error(buf);
        goto cleanup;
    }

    {
        long http_code = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

        AngaraObject body_str = ang_api->string_no_copy(chunk.buffer, chunk.size);
        AngaraObject result_record = ang_api->record_new();
        ang_api->record_set(result_record, "status", ang_i64(http_code));
        ang_api->record_set(result_record, "body", body_str);
        ang_api->decref(body_str);

        // Cleanup refs
        ang_api->decref(url_obj); ang_api->decref(method_obj);
        ang_api->decref(headers_obj); ang_api->decref(body_obj);
        curl_easy_cleanup(curl_handle);
        if (headers) curl_slist_free_all(headers);
        return result_record;
    }

cleanup:
    curl_easy_cleanup(curl_handle);
    if (headers) curl_slist_free_all(headers);
    ang_api->decref(url_obj); ang_api->decref(method_obj);
    ang_api->decref(headers_obj); ang_api->decref(body_obj);
    free(chunk.buffer);
    return ang_nil();
}

static const AngaraFuncDef HTTP_EXPORTS[] = {
    {"request", Angara_http_request, "{}->{}", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(http) {
    ang_api = api;
    *def_count = (sizeof(HTTP_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return HTTP_EXPORTS;
}
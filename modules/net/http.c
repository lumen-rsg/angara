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

/* apply TLS options from a record to a curl handle */
static void apply_tls_options(CURL* curl_handle, AngaraObject tls_opts) {
    if (!IS_REC(tls_opts)) return;

    AngaraObject v = ang_api->record_get(tls_opts, "verify");
    if (ang_is_bool(v)) {
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, ang_as_bool(v) ? 1L : 0L);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, ang_as_bool(v) ? 2L : 0L);
    }
    ang_api->decref(v);

    v = ang_api->record_get(tls_opts, "ca_bundle");
    if (IS_STR(v)) curl_easy_setopt(curl_handle, CURLOPT_CAINFO, ang_api->as_cstr(v));
    ang_api->decref(v);

    v = ang_api->record_get(tls_opts, "client_cert");
    if (IS_STR(v)) curl_easy_setopt(curl_handle, CURLOPT_SSLCERT, ang_api->as_cstr(v));
    ang_api->decref(v);

    v = ang_api->record_get(tls_opts, "client_key");
    if (IS_STR(v)) curl_easy_setopt(curl_handle, CURLOPT_SSLKEY, ang_api->as_cstr(v));
    ang_api->decref(v);

    v = ang_api->record_get(tls_opts, "client_key_pass");
    if (IS_STR(v)) curl_easy_setopt(curl_handle, CURLOPT_KEYPASSWD, ang_api->as_cstr(v));
    ang_api->decref(v);
}

AngaraObject Angara_http_request(int arg_count, AngaraObject args[]) {
    if (arg_count < 1) { ang_api->throw_error("http.request: expected 1 argument"); return ang_nil(); }
    CURLcode res = CURLE_OK;
    struct curl_slist* headers = NULL;

    AngaraObject options = args[0];

    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) { ang_api->throw_error("Failed to initialize libcurl."); return ang_nil(); }

    MemoryStruct chunk = { .buffer = malloc(1), .size = 0 };
    if (!chunk.buffer) { curl_easy_cleanup(curl_handle); ang_api->throw_error("OOM."); return ang_nil(); }
    chunk.buffer[0] = '\0';

    AngaraObject url_obj = ang_api->record_get(options, "url");
    AngaraObject method_obj = ang_api->record_get(options, "method");
    AngaraObject headers_obj = ang_api->record_get(options, "headers");
    AngaraObject body_obj = ang_api->record_get(options, "body");
    AngaraObject tls_obj  = ang_api->record_get(options, "tls");

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
    apply_tls_options(curl_handle, tls_obj);

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

        ang_api->decref(url_obj); ang_api->decref(method_obj);
        ang_api->decref(headers_obj); ang_api->decref(body_obj); ang_api->decref(tls_obj);
        curl_easy_cleanup(curl_handle);
        if (headers) curl_slist_free_all(headers);
        return result_record;
    }

cleanup:
    curl_easy_cleanup(curl_handle);
    if (headers) curl_slist_free_all(headers);
    ang_api->decref(url_obj); ang_api->decref(method_obj);
    ang_api->decref(headers_obj); ang_api->decref(body_obj); ang_api->decref(tls_obj);
    free(chunk.buffer);
    return ang_nil();
}

static AngaraObject http_simple_request(const char* method, const char* url,
                                         const char* body, size_t body_len,
                                         AngaraObject headers_rec) {
    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) { ang_api->throw_error("Failed to initialize libcurl."); return ang_nil(); }

    MemoryStruct chunk = { .buffer = malloc(1), .size = 0 };
    if (!chunk.buffer) { curl_easy_cleanup(curl_handle); return ang_nil(); }
    chunk.buffer[0] = '\0';

    struct curl_slist* headers = NULL;
    RequestBody req_body = { .body = body, .size = body_len, .sent = 0 };

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "angara-http-client/1.0");
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
    /* enable TLS by default; caller can override via http.request() with tls options */
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl_handle, CURLOPT_UPLOAD, 1L);
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (strcmp(method, "HEAD") == 0) {
        curl_easy_setopt(curl_handle, CURLOPT_NOBODY, 1L);
    }

    if (body && body_len > 0) {
        curl_easy_setopt(curl_handle, CURLOPT_READFUNCTION, read_callback);
        curl_easy_setopt(curl_handle, CURLOPT_READDATA, &req_body);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
    }

    if (IS_REC(headers_rec)) {
        size_t hcount = ang_api->record_len(headers_rec);
        for (size_t i = 0; i < hcount; ++i) {
            const char* key = ang_api->record_key_at(headers_rec, i);
            AngaraObject val = ang_api->record_val_at(headers_rec, i);
            char header_string[1024];
            snprintf(header_string, sizeof(header_string), "%s: %s", key, IS_STR(val) ? ang_api->as_cstr(val) : "");
            headers = curl_slist_append(headers, header_string);
            ang_api->decref(val);
        }
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        char buf[256]; snprintf(buf, 256, "http %s failed: %s", method, curl_easy_strerror(res));
        ang_api->throw_error(buf);
        curl_easy_cleanup(curl_handle);
        if (headers) curl_slist_free_all(headers);
        free(chunk.buffer);
        return ang_nil();
    }

    long http_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

    AngaraObject body_str = ang_api->string_no_copy(chunk.buffer, chunk.size);
    AngaraObject result_record = ang_api->record_new();
    ang_api->record_set(result_record, "status", ang_i64(http_code));
    ang_api->record_set(result_record, "body", body_str);
    ang_api->decref(body_str);

    curl_easy_cleanup(curl_handle);
    if (headers) curl_slist_free_all(headers);
    return result_record;
}

AngaraObject Angara_http_get(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) { ang_api->throw_error("http.get(url) expects a string."); return ang_nil(); }
    AngaraObject headers = (arg_count >= 2 && IS_REC(args[1])) ? args[1] : ang_api->record_new();
    AngaraObject result = http_simple_request("GET", ang_api->as_cstr(args[0]), NULL, 0, headers);
    return result;
}

AngaraObject Angara_http_post(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) { ang_api->throw_error("http.post(url, body?) expects a string."); return ang_nil(); }
    const char* body = NULL; size_t body_len = 0;
    if (arg_count >= 2 && IS_STR(args[1])) { body = ang_api->as_cstr(args[1]); body_len = ang_api->str_len(args[1]); }
    AngaraObject headers = (arg_count >= 3 && IS_REC(args[2])) ? args[2] : ang_api->record_new();
    return http_simple_request("POST", ang_api->as_cstr(args[0]), body, body_len, headers);
}

AngaraObject Angara_http_put(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) { ang_api->throw_error("http.put(url, body?) expects a string."); return ang_nil(); }
    const char* body = NULL; size_t body_len = 0;
    if (arg_count >= 2 && IS_STR(args[1])) { body = ang_api->as_cstr(args[1]); body_len = ang_api->str_len(args[1]); }
    AngaraObject headers = (arg_count >= 3 && IS_REC(args[2])) ? args[2] : ang_api->record_new();
    return http_simple_request("PUT", ang_api->as_cstr(args[0]), body, body_len, headers);
}

AngaraObject Angara_http_delete(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) { ang_api->throw_error("http.delete(url) expects a string."); return ang_nil(); }
    AngaraObject headers = (arg_count >= 2 && IS_REC(args[1])) ? args[1] : ang_api->record_new();
    return http_simple_request("DELETE", ang_api->as_cstr(args[0]), NULL, 0, headers);
}

AngaraObject Angara_http_patch(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) { ang_api->throw_error("http.patch(url, body?) expects a string."); return ang_nil(); }
    const char* body = NULL; size_t body_len = 0;
    if (arg_count >= 2 && IS_STR(args[1])) { body = ang_api->as_cstr(args[1]); body_len = ang_api->str_len(args[1]); }
    AngaraObject headers = (arg_count >= 3 && IS_REC(args[2])) ? args[2] : ang_api->record_new();
    return http_simple_request("PATCH", ang_api->as_cstr(args[0]), body, body_len, headers);
}

AngaraObject Angara_http_head(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) { ang_api->throw_error("http.head(url) expects a string."); return ang_nil(); }
    AngaraObject headers = (arg_count >= 2 && IS_REC(args[1])) ? args[1] : ang_api->record_new();
    return http_simple_request("HEAD", ang_api->as_cstr(args[0]), NULL, 0, headers);
}

static const AngaraFuncDef HTTP_EXPORTS[] = {
    {"request", Angara_http_request, "{}->{}",  NULL},
    {"get",     Angara_http_get,     "s{}?->{}", NULL},
    {"post",    Angara_http_post,    "ss?{}?->{}", NULL},
    {"put",     Angara_http_put,     "ss?{}?->{}", NULL},
    {"delete",  Angara_http_delete,  "s{}?->{}", NULL},
    {"patch",   Angara_http_patch,   "ss?{}?->{}", NULL},
    {"head",    Angara_http_head,    "s{}?->{}", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(http) {
    ang_api = api;
    *def_count = (sizeof(HTTP_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return HTTP_EXPORTS;
}
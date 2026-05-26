/// Angara Matter module — Matter smart-home protocol via REST API bridge. Depends: libcurl.
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

typedef struct { char* buffer; size_t size; } HttpResponse;

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    HttpResponse* resp = (HttpResponse*)userp;
    char* ptr = realloc(resp->buffer, resp->size + realsize + 1);
    if (!ptr) return 0;
    resp->buffer = ptr;
    memcpy(&(resp->buffer[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->buffer[resp->size] = 0;
    return realsize;
}

typedef struct {
    char* base_url;
    CURL* curl;
    pthread_mutex_t curl_mutex;
} MatterControllerData;

static void finalize_controller(void* data) {
    MatterControllerData* ctrl = (MatterControllerData*)data;
    if (ctrl->curl) curl_easy_cleanup(ctrl->curl);
    free(ctrl->base_url);
    pthread_mutex_destroy(&ctrl->curl_mutex);
    free(ctrl);
}

static AngaraObject http_get(MatterControllerData* ctrl, const char* path) {
    pthread_mutex_lock(&ctrl->curl_mutex);

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", ctrl->base_url, path);

    curl_easy_reset(ctrl->curl);
    curl_easy_setopt(ctrl->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctrl->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(ctrl->curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(ctrl->curl, CURLOPT_CONNECTTIMEOUT, 10L);

    HttpResponse resp = { .buffer = malloc(1), .size = 0 };
    resp.buffer[0] = '\0';
    curl_easy_setopt(ctrl->curl, CURLOPT_WRITEDATA, &resp);

    CURLcode res = curl_easy_perform(ctrl->curl);
    if (res != CURLE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "matter: HTTP GET %s failed: %s", path, curl_easy_strerror(res));
        free(resp.buffer);
        pthread_mutex_unlock(&ctrl->curl_mutex);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    long http_code = 0;
    curl_easy_getinfo(ctrl->curl, CURLINFO_RESPONSE_CODE, &http_code);
    pthread_mutex_unlock(&ctrl->curl_mutex);

    AngaraObject body = ang_api->string_no_copy(resp.buffer, resp.size);
    return body;
}

static AngaraObject http_post(MatterControllerData* ctrl, const char* path, const char* json_body) {
    pthread_mutex_lock(&ctrl->curl_mutex);

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", ctrl->base_url, path);

    curl_easy_reset(ctrl->curl);
    curl_easy_setopt(ctrl->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctrl->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(ctrl->curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(ctrl->curl, CURLOPT_CONNECTTIMEOUT, 10L);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(ctrl->curl, CURLOPT_HTTPHEADER, headers);

    if (json_body) {
        curl_easy_setopt(ctrl->curl, CURLOPT_POST, 1L);
        curl_easy_setopt(ctrl->curl, CURLOPT_POSTFIELDS, json_body);
    }

    HttpResponse resp = { .buffer = malloc(1), .size = 0 };
    resp.buffer[0] = '\0';
    curl_easy_setopt(ctrl->curl, CURLOPT_WRITEDATA, &resp);

    CURLcode res = curl_easy_perform(ctrl->curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "matter: HTTP POST %s failed: %s", path, curl_easy_strerror(res));
        free(resp.buffer);
        pthread_mutex_unlock(&ctrl->curl_mutex);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    long http_code = 0;
    curl_easy_getinfo(ctrl->curl, CURLINFO_RESPONSE_CODE, &http_code);
    pthread_mutex_unlock(&ctrl->curl_mutex);

    AngaraObject body = ang_api->string_no_copy(resp.buffer, resp.size);
    return body;
}

static AngaraObject http_put(MatterControllerData* ctrl, const char* path, const char* json_body) {
    pthread_mutex_lock(&ctrl->curl_mutex);

    char url[1024];
    snprintf(url, sizeof(url), "%s%s", ctrl->base_url, path);

    curl_easy_reset(ctrl->curl);
    curl_easy_setopt(ctrl->curl, CURLOPT_URL, url);
    curl_easy_setopt(ctrl->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(ctrl->curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(ctrl->curl, CURLOPT_CONNECTTIMEOUT, 10L);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(ctrl->curl, CURLOPT_HTTPHEADER, headers);

    if (json_body) {
        curl_easy_setopt(ctrl->curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(ctrl->curl, CURLOPT_POSTFIELDS, json_body);
    }

    HttpResponse resp = { .buffer = malloc(1), .size = 0 };
    resp.buffer[0] = '\0';
    curl_easy_setopt(ctrl->curl, CURLOPT_WRITEDATA, &resp);

    CURLcode res = curl_easy_perform(ctrl->curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "matter: HTTP PUT %s failed: %s", path, curl_easy_strerror(res));
        free(resp.buffer);
        pthread_mutex_unlock(&ctrl->curl_mutex);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    long http_code = 0;
    curl_easy_getinfo(ctrl->curl, CURLINFO_RESPONSE_CODE, &http_code);
    pthread_mutex_unlock(&ctrl->curl_mutex);

    AngaraObject body = ang_api->string_no_copy(resp.buffer, resp.size);
    return body;
}

static char* record_to_json(AngaraObject rec) {
    size_t capacity = 1024;
    char* json = (char*)malloc(capacity);
    size_t len = 1;
    json[0] = '{';

    size_t rlen = ang_api->record_len(rec);
    bool first = true;

    for (size_t i = 0; i < rlen; i++) {
        const char* key = ang_api->record_key_at(rec, i);
        AngaraObject val = ang_api->record_val_at(rec, i);

        size_t needed = len + strlen(key) * 2 + 64;
        if (needed >= capacity) {
            capacity = needed + 256;
            json = (char*)realloc(json, capacity);
        }

        if (!first) { json[len++] = ','; }
        first = false;

        len += snprintf(json + len, capacity - len, "\"%s\":", key);

        if (ang_is_nil(val)) {
            len += snprintf(json + len, capacity - len, "null");
        } else if (ang_is_bool(val)) {
            len += snprintf(json + len, capacity - len, ang_as_bool(val) ? "true" : "false");
        } else if (ang_is_i64(val)) {
            len += snprintf(json + len, capacity - len, "%lld", (long long)ang_as_i64(val));
        } else if (ang_is_f64(val)) {
            len += snprintf(json + len, capacity - len, "%g", ang_as_f64(val));
        } else if (IS_STR(val)) {
            const char* s = ang_api->as_cstr(val);
            size_t slen = strlen(s);
            while (len + slen * 2 + 4 >= capacity) { capacity *= 2; json = (char*)realloc(json, capacity); }
            json[len++] = '"';
            for (size_t j = 0; j < slen; j++) {
                char c = s[j];
                if (c == '"' || c == '\\') { json[len++] = '\\'; json[len++] = c; }
                else if (c == '\n') { json[len++] = '\\'; json[len++] = 'n'; }
                else if (c == '\r') { json[len++] = '\\'; json[len++] = 'r'; }
                else if (c == '\t') { json[len++] = '\\'; json[len++] = 't'; }
                else { json[len++] = c; }
            }
            json[len++] = '"';
        } else if (IS_REC(val)) {
            char* sub = record_to_json(val);
            size_t sublen = strlen(sub);
            while (len + sublen + 2 >= capacity) { capacity *= 2; json = (char*)realloc(json, capacity); }
            memcpy(json + len, sub, sublen);
            len += sublen;
            free(sub);
        } else {
            len += snprintf(json + len, capacity - len, "null");
        }

        ang_api->decref(val);
    }

    while (len + 4 >= capacity) { capacity *= 2; json = (char*)realloc(json, capacity); }
    json[len++] = '}';
    json[len] = '\0';
    return json;
}

AngaraObject Angara_matter_connect(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("matter.connect(host, options?) expects a string URL.");
        return ang_nil();
    }

    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }

    const char* url = ang_api->as_cstr(args[0]);
    size_t url_len = strlen(url);

    MatterControllerData* ctrl = (MatterControllerData*)calloc(1, sizeof(MatterControllerData));
    ctrl->base_url = (char*)malloc(url_len + 2);
    memcpy(ctrl->base_url, url, url_len);
    if (url_len > 0 && url[url_len - 1] == '/') {
        ctrl->base_url[url_len] = '\0';
    } else {
        ctrl->base_url[url_len] = '\0';
    }
    pthread_mutex_init(&ctrl->curl_mutex, NULL);
    ctrl->curl = curl_easy_init();
    if (!ctrl->curl) {
        free(ctrl->base_url);
        pthread_mutex_destroy(&ctrl->curl_mutex);
        free(ctrl);
        ang_api->throw_error("matter.connect: failed to initialize CURL.");
        return ang_nil();
    }

    return ang_api->native_instance_new(ctrl, finalize_controller, "Controller");
}

AngaraObject Angara_Controller_devices(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    AngaraObject body = http_get(ctrl, "/api/v1/devices");
    if (ang_is_nil(body)) return ang_api->list_new();

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "response", body);
    ang_api->record_set(rec, "format", ang_api->string("json"));
    ang_api->decref(body);

    AngaraObject list = ang_api->list_new();
    ang_api->list_push(list, rec);
    ang_api->decref(rec);
    return list;
}

AngaraObject Angara_Controller_commission(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("Controller.commission(setupCode, nodeId?) expects a string.");
        return ang_nil();
    }

    const char* setup_code = ang_api->as_cstr(args[1]);
    int64_t node_id = 0;
    if (arg_count >= 3 && ang_is_i64(args[2])) node_id = ang_as_i64(args[2]);

    char json[512];
    if (node_id > 0) {
        snprintf(json, sizeof(json), "{\"setupCode\":\"%s\",\"nodeId\":%lld}", setup_code, (long long)node_id);
    } else {
        snprintf(json, sizeof(json), "{\"setupCode\":\"%s\"}", setup_code);
    }

    AngaraObject body = http_post(ctrl, "/api/v1/commission", json);
    if (!ang_is_nil(body)) ang_api->decref(body);
    return ang_nil();
}

AngaraObject Angara_Controller_read(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    if (arg_count < 4 || !ang_is_i64(args[1]) || !ang_is_i64(args[2]) || !ang_is_i64(args[3])) {
        ang_api->throw_error("Controller.read(nodeId, endpoint, cluster, attribute?) expects integers.");
        return ang_nil();
    }

    int64_t node_id = ang_as_i64(args[1]);
    int64_t endpoint = ang_as_i64(args[2]);
    int64_t cluster = ang_as_i64(args[3]);
    int64_t attribute = 0;
    if (arg_count >= 5 && ang_is_i64(args[4])) attribute = ang_as_i64(args[4]);

    char path[512];
    snprintf(path, sizeof(path), "/api/v1/read?nodeId=%lld&endpoint=%lld&cluster=%lld&attribute=%lld",
             (long long)node_id, (long long)endpoint, (long long)cluster, (long long)attribute);

    return http_get(ctrl, path);
}

AngaraObject Angara_Controller_write(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    if (arg_count < 5 || !ang_is_i64(args[1]) || !ang_is_i64(args[2]) ||
        !ang_is_i64(args[3]) || !ang_is_i64(args[4])) {
        ang_api->throw_error("Controller.write(nodeId, endpoint, cluster, attribute, value) expects args.");
        return ang_nil();
    }

    int64_t node_id = ang_as_i64(args[1]);
    int64_t endpoint = ang_as_i64(args[2]);
    int64_t cluster = ang_as_i64(args[3]);
    int64_t attribute = ang_as_i64(args[4]);

    char val_json[256];
    if (arg_count >= 6) {
        if (IS_STR(args[5])) {
            snprintf(val_json, sizeof(val_json), "\"%s\"", ang_api->as_cstr(args[5]));
        } else if (ang_is_i64(args[5])) {
            snprintf(val_json, sizeof(val_json), "%lld", (long long)ang_as_i64(args[5]));
        } else if (ang_is_bool(args[5])) {
            snprintf(val_json, sizeof(val_json), "%s", ang_as_bool(args[5]) ? "true" : "false");
        } else if (IS_REC(args[5])) {
            char* sub = record_to_json(args[5]);
            snprintf(val_json, sizeof(val_json), "%s", sub);
            free(sub);
        } else {
            snprintf(val_json, sizeof(val_json), "null");
        }
    } else {
        snprintf(val_json, sizeof(val_json), "null");
    }

    char json[1024];
    snprintf(json, sizeof(json),
             "{\"nodeId\":%lld,\"endpoint\":%lld,\"cluster\":%lld,\"attribute\":%lld,\"value\":%s}",
             (long long)node_id, (long long)endpoint, (long long)cluster,
             (long long)attribute, val_json);

    AngaraObject body = http_put(ctrl, "/api/v1/write", json);
    if (!ang_is_nil(body)) ang_api->decref(body);
    return ang_nil();
}

AngaraObject Angara_Controller_command(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    if (arg_count < 4 || !ang_is_i64(args[1]) || !ang_is_i64(args[2]) || !IS_STR(args[3])) {
        ang_api->throw_error("Controller.command(nodeId, endpoint, command, args?) expects args.");
        return ang_nil();
    }

    int64_t node_id = ang_as_i64(args[1]);
    int64_t endpoint = ang_as_i64(args[2]);
    const char* command = ang_api->as_cstr(args[3]);

    char* payload_json = NULL;
    if (arg_count >= 5 && IS_REC(args[4])) {
        payload_json = record_to_json(args[4]);
    }

    char* json;
    if (payload_json) {
        size_t jlen = 256 + strlen(command) + strlen(payload_json);
        json = (char*)malloc(jlen);
        snprintf(json, jlen,
                 "{\"nodeId\":%lld,\"endpoint\":%lld,\"command\":\"%s\",\"payload\":%s}",
                 (long long)node_id, (long long)endpoint, command, payload_json);
        free(payload_json);
    } else {
        json = (char*)malloc(256 + strlen(command));
        snprintf(json, 256 + strlen(command),
                 "{\"nodeId\":%lld,\"endpoint\":%lld,\"command\":\"%s\",\"payload\":{}}",
                 (long long)node_id, (long long)endpoint, command);
    }

    AngaraObject body = http_post(ctrl, "/api/v1/command", json);
    free(json);
    return body;
}

AngaraObject Angara_Controller_subscribe(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    if (arg_count < 6 || !ang_is_i64(args[1]) || !ang_is_i64(args[2]) ||
        !ang_is_i64(args[3]) || !ang_is_i64(args[4])) {
        ang_api->throw_error("Controller.subscribe(nodeId, endpoint, cluster, attribute, min, max) expects args.");
        return ang_nil();
    }

    int64_t node_id = ang_as_i64(args[1]);
    int64_t endpoint = ang_as_i64(args[2]);
    int64_t cluster = ang_as_i64(args[3]);
    int64_t attribute = ang_as_i64(args[4]);
    int64_t min_interval = ang_as_i64(args[5]);
    int64_t max_interval = (arg_count >= 7 && ang_is_i64(args[6])) ? ang_as_i64(args[6]) : 300;

    char json[512];
    snprintf(json, sizeof(json),
             "{\"nodeId\":%lld,\"endpoint\":%lld,\"cluster\":%lld,\"attribute\":%lld,\"minInterval\":%lld,\"maxInterval\":%lld}",
             (long long)node_id, (long long)endpoint, (long long)cluster,
             (long long)attribute, (long long)min_interval, (long long)max_interval);

    AngaraObject body = http_post(ctrl, "/api/v1/subscribe", json);
    if (!ang_is_nil(body)) ang_api->decref(body);
    return ang_nil();
}

AngaraObject Angara_Controller_decommission(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    if (arg_count < 2 || !ang_is_i64(args[1])) {
        ang_api->throw_error("Controller.decommission(nodeId) expects an integer.");
        return ang_nil();
    }

    char json[256];
    snprintf(json, sizeof(json), "{\"nodeId\":%lld}", (long long)ang_as_i64(args[1]));

    AngaraObject body = http_post(ctrl, "/api/v1/decommission", json);
    if (!ang_is_nil(body)) ang_api->decref(body);
    return ang_nil();
}

AngaraObject Angara_Controller_disconnect(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) return ang_nil();
    return ang_nil();
}

AngaraObject Angara_Controller_is_connected(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl || !ctrl->curl) return ang_bool(false);
    AngaraObject body = http_get(ctrl, "/api/v1/health");
    bool ok = !ang_is_nil(body);
    if (ok) ang_api->decref(body);
    return ang_bool(ok);
}

AngaraObject Angara_Controller_info(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }
    return http_get(ctrl, "/api/v1/info");
}

AngaraObject Angara_Controller_node_description(int arg_count, AngaraObject* args) {
    MatterControllerData* ctrl = (MatterControllerData*)ang_api->native_instance_data(args[0]);
    if (!ctrl) { ang_api->throw_error("matter: invalid controller."); return ang_nil(); }

    if (arg_count < 2 || !ang_is_i64(args[1])) {
        ang_api->throw_error("Controller.node_description(nodeId) expects an integer.");
        return ang_nil();
    }

    char path[256];
    snprintf(path, sizeof(path), "/api/v1/node/%lld", (long long)ang_as_i64(args[1]));
    return http_get(ctrl, path);
}

static const AngaraMethodDef CONTROLLER_METHODS[] = {
    {"commission",      (AngaraMethodFn)Angara_Controller_commission,       "si?->n"},
    {"devices",         (AngaraMethodFn)Angara_Controller_devices,          "->l{}"},
    {"read",            (AngaraMethodFn)Angara_Controller_read,             "iiii?->s"},
    {"write",           (AngaraMethodFn)Angara_Controller_write,            "iiiia->n"},
    {"command",         (AngaraMethodFn)Angara_Controller_command,          "iisl->s"},
    {"subscribe",       (AngaraMethodFn)Angara_Controller_subscribe,        "iiiiii->n"},
    {"decommission",    (AngaraMethodFn)Angara_Controller_decommission,     "i->n"},
    {"disconnect",      (AngaraMethodFn)Angara_Controller_disconnect,       "->n"},
    {"is_connected",    (AngaraMethodFn)Angara_Controller_is_connected,     "->b"},
    {"info",            (AngaraMethodFn)Angara_Controller_info,             "->s"},
    {"node_description",(AngaraMethodFn)Angara_Controller_node_description, "i->s"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef CONTROLLER_CLASS_DEF = { "Controller", NULL, CONTROLLER_METHODS };

static const AngaraFuncDef MATTER_EXPORTS[] = {
    {"connect", Angara_matter_connect, "s{}?->Controller", &CONTROLLER_CLASS_DEF},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(matter) {
    ang_api = api;
    *def_count = (sizeof(MATTER_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return MATTER_EXPORTS;
}
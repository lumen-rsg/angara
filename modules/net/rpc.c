#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>

#include "Angara.h"
#include "json_bridge.h"


#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


typedef struct RpcRequest {
    int64_t         id;
    char*           method;
    AngaraObject    params;
    int             client_fd;
    struct RpcRequest* next;
} RpcRequest;


typedef struct {
    int             listen_fd;
    uint16_t        port;
    RpcRequest*     req_head;
    RpcRequest*     req_tail;
    pthread_mutex_t req_mutex;
} RpcServer;

static void finalize_server(void* data) {
    RpcServer* srv = (RpcServer*)data;
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    RpcRequest* cur = srv->req_head;
    while (cur) {
        RpcRequest* next = cur->next;
        free(cur->method);
        ang_api->decref(cur->params);
        close(cur->client_fd);
        free(cur);
        cur = next;
    }
    pthread_mutex_destroy(&srv->req_mutex);
    free(srv);
}


static AngaraObject json_to_angara(JsonHandle handle) {
    if (!handle) return ang_nil();
    if (json_bridge_is_object(handle)) {
        AngaraObject rec = ang_api->record_new();
        size_t sz = json_bridge_object_size(handle);
        for (size_t i = 0; i < sz; ++i) {
            const char* key = json_bridge_object_get_key_at(handle, i);
            JsonHandle val = json_bridge_object_get_value_at(handle, i);
            if (key && val) {
                AngaraObject av = json_to_angara(val);
                ang_api->record_set(rec, key, av);
                ang_api->decref(av);
            }
            if (key) free((void*)key);
        }
        return rec;
    }
    if (json_bridge_is_array(handle)) {
        AngaraObject list = ang_api->list_new();
        size_t sz = json_bridge_array_size(handle);
        for (size_t i = 0; i < sz; ++i) {
            JsonHandle elem = json_bridge_array_get_element(handle, i);
            AngaraObject av = json_to_angara(elem);
            ang_api->list_push(list, av);
            ang_api->decref(av);
        }
        return list;
    }
    if (json_bridge_is_string(handle)) {
        const char* s = json_bridge_get_string(handle);
        AngaraObject obj = ang_api->string(s ? s : "");
        if (s) free((void*)s);
        return obj;
    }
    if (json_bridge_is_number(handle))  return ang_f64(json_bridge_get_number(handle));
    if (json_bridge_is_boolean(handle)) return ang_bool(json_bridge_get_boolean(handle));
    if (json_bridge_is_null(handle))    return ang_nil();
    return ang_nil();
}

static JsonHandle angara_to_json(AngaraObject obj) {
    if (ang_is_nil(obj))   return json_bridge_new_null();
    if (ang_is_bool(obj))  return json_bridge_new_bool(ang_as_bool(obj));
    if (ang_is_i64(obj))   return json_bridge_new_number((double)ang_as_i64(obj));
    if (ang_is_f64(obj))   return json_bridge_new_number(ang_as_f64(obj));
    if (ang_is_obj(obj)) {
        int32_t otype = ang_api->obj_type(obj);
        if (otype == ANG_OBJ_STRING) {
            return json_bridge_new_string(ang_api->as_cstr(obj));
        }
        if (otype == ANG_OBJ_LIST) {
            JsonHandle arr = json_bridge_new_array();
            size_t len = ang_api->list_len(obj);
            for (size_t i = 0; i < len; ++i) {
                AngaraObject elem = ang_api->list_get(obj, (int64_t)i);
                JsonHandle jh = angara_to_json(elem);
                json_bridge_array_add(arr, jh);
                json_bridge_free(jh);
                ang_api->decref(elem);
            }
            return arr;
        }
        if (otype == ANG_OBJ_RECORD) {
            JsonHandle obj_h = json_bridge_new_object();
            size_t len = ang_api->record_len(obj);
            for (size_t i = 0; i < len; ++i) {
                const char* key = ang_api->record_key_at(obj, i);
                AngaraObject val = ang_api->record_val_at(obj, i);
                JsonHandle jh = angara_to_json(val);
                json_bridge_object_add(obj_h, key, jh);
                json_bridge_free(jh);
                ang_api->decref(val);
            }
            return obj_h;
        }
    }
    return json_bridge_new_null();
}


typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} ReadBuffer;

static char* read_ndjson_message(int fd, int* out_closed) {
    *out_closed = 0;
    ReadBuffer buf;
    buf.cap = 4096;
    buf.len = 0;
    buf.data = (char*)malloc(buf.cap);
    if (!buf.data) return NULL;

    while (1) {
        char c;
        ssize_t n = recv(fd, &c, 1, MSG_DONTWAIT);
        if (n == 0) {
            *out_closed = 1;
            free(buf.data);
            return NULL;
        }
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            }
            free(buf.data);
            return NULL;
        }
        if (c == '\n') {
            break;
        }
        if (buf.len + 1 >= buf.cap) {
            buf.cap *= 2;
            char* new_data = (char*)realloc(buf.data, buf.cap);
            if (!new_data) { free(buf.data); return NULL; }
            buf.data = new_data;
        }
        buf.data[buf.len++] = c;
    }

    if (buf.len == 0) {
        free(buf.data);
        return NULL;
    }

    buf.data[buf.len] = '\0';
    return buf.data;
}


static int send_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}


AngaraObject Angara_rpc_create_server(int argc, AngaraObject args[]) {
    if (argc < 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("rpc.create_server(port) requires an integer port.");
        return ang_nil();
    }

    int port = (int)ang_as_i64(args[0]);

    RpcServer* srv = (RpcServer*)calloc(1, sizeof(RpcServer));
    if (!srv) { ang_api->throw_error("rpc: out of memory."); return ang_nil(); }

    srv->port = (uint16_t)port;
    srv->listen_fd = -1;
    pthread_mutex_init(&srv->req_mutex, NULL);

    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        ang_api->throw_error("rpc: failed to create socket.");
        finalize_server(srv);
        return ang_nil();
    }

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(srv->port);

    if (bind(srv->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "rpc: failed to bind on port %d: %s", port, strerror(errno));
        ang_api->throw_error(buf);
        finalize_server(srv);
        return ang_nil();
    }

    if (listen(srv->listen_fd, 32) < 0) {
        ang_api->throw_error("rpc: failed to listen.");
        finalize_server(srv);
        return ang_nil();
    }

    set_nonblocking(srv->listen_fd);

    return ang_api->native_instance_new(srv, finalize_server, "RpcServer");
}


AngaraObject Angara_RpcServer_service(int argc, AngaraObject args[]) {
    if (argc < 1) return ang_nil();
    RpcServer* srv = (RpcServer*)ang_api->native_instance_data(args[0]);
    if (!srv || srv->listen_fd < 0) return ang_nil();

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(srv->listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) break;
        set_nonblocking(client_fd);
        close(client_fd);  /* service() drains the accept queue; poll_clients() returns fds */
    }

    return ang_nil();
}


AngaraObject Angara_RpcServer_poll_clients(int argc, AngaraObject args[]) {
    if (argc < 1) return ang_nil();
    RpcServer* srv = (RpcServer*)ang_api->native_instance_data(args[0]);
    if (!srv || srv->listen_fd < 0) return ang_nil();

    AngaraObject result = ang_api->list_new();

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(srv->listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) break;
        set_nonblocking(client_fd);
        ang_api->list_push(result, ang_i64(client_fd));
    }

    return result;
}


AngaraObject Angara_RpcServer_read_request(int argc, AngaraObject args[]) {
    if (argc < 2) return ang_nil();
    if (!ang_is_i64(args[0])) return ang_nil();
    RpcServer* srv = (RpcServer*)ang_api->native_instance_data(args[0]);
    if (!srv) return ang_nil();

    int client_fd = (int)ang_as_i64(args[1]);

    int closed = 0;
    char* json_str = read_ndjson_message(client_fd, &closed);
    if (!json_str) {
        if (closed) close(client_fd);
        return ang_nil();
    }

    char* error_msg = NULL;
    JsonHandle root = json_bridge_parse(json_str, &error_msg);
    free(json_str);

    if (!root) {
        if (error_msg) { free(error_msg); }
        return ang_nil();
    }

    if (!json_bridge_is_object(root)) {
        json_bridge_free(root);
        return ang_nil();
    }


    AngaraObject result = ang_api->record_new();

    JsonHandle method_val = NULL;
    size_t obj_sz = json_bridge_object_size(root);
    const char* method_str = NULL;
    for (size_t i = 0; i < obj_sz; ++i) {
        const char* key = json_bridge_object_get_key_at(root, i);
        if (key && strcmp(key, "method") == 0) {
            method_val = json_bridge_object_get_value_at(root, i);
            if (method_val && json_bridge_is_string(method_val)) {
                const char* ms = json_bridge_get_string(method_val);
                method_str = ms;
                ang_api->record_set(result, "method", ang_api->string(ms ? ms : ""));
                if (ms) free((void*)ms);
            }
        } else if (key && strcmp(key, "id") == 0) {
            JsonHandle id_val = json_bridge_object_get_value_at(root, i);
            if (id_val) {
                if (json_bridge_is_number(id_val)) {
                    ang_api->record_set(result, "id", ang_i64((int64_t)json_bridge_get_number(id_val)));
                } else if (json_bridge_is_string(id_val)) {
                    const char* sid = json_bridge_get_string(id_val);
                    ang_api->record_set(result, "id", ang_api->string(sid ? sid : ""));
                    if (sid) free((void*)sid);
                } else if (json_bridge_is_null(id_val)) {
                }
            }
        } else if (key && strcmp(key, "params") == 0) {
            JsonHandle params_val = json_bridge_object_get_value_at(root, i);
            if (params_val) {
                AngaraObject params = json_to_angara(params_val);
                ang_api->record_set(result, "params", params);
                ang_api->decref(params);
            }
        }
        if (key) free((void*)key);
    }

    json_bridge_free(root);

    if (!method_str) {
        return ang_nil();
    }

    ang_api->record_set(result, "client_fd", ang_i64(client_fd));

    return result;
}


AngaraObject Angara_RpcServer_respond(int argc, AngaraObject args[]) {
    if (argc < 4) {
        ang_api->throw_error("rpc.respond expects (id, result, client_fd).");
        return ang_nil();
    }

    int client_fd = -1;
    if (ang_is_i64(args[3])) client_fd = (int)ang_as_i64(args[3]);
    if (client_fd < 0) return ang_nil();

    JsonHandle resp = json_bridge_new_object();
    json_bridge_object_add(resp, "jsonrpc", json_bridge_new_string("2.0"));

    if (ang_is_i64(args[1])) {
        json_bridge_object_add(resp, "id", json_bridge_new_number((double)ang_as_i64(args[1])));
    } else if (IS_STR(args[1])) {
        json_bridge_object_add(resp, "id", json_bridge_new_string(ang_api->as_cstr(args[1])));
    } else {
        json_bridge_object_add(resp, "id", json_bridge_new_null());
    }

    JsonHandle result_json = angara_to_json(args[2]);
    json_bridge_object_add(resp, "result", result_json);
    json_bridge_free(result_json);

    const char* resp_str = json_bridge_stringify(resp);
    json_bridge_free(resp);

    if (resp_str) {
        size_t len = strlen(resp_str);
        char* send_buf = (char*)malloc(len + 2);
        if (send_buf) {
            memcpy(send_buf, resp_str, len);
            send_buf[len] = '\n';
            send_buf[len + 1] = '\0';
            send_all(client_fd, send_buf, len + 1);
            free(send_buf);
        }
        json_bridge_free_string((char*)resp_str);
    }

    close(client_fd);

    return ang_nil();
}


AngaraObject Angara_RpcServer_close(int argc, AngaraObject args[]) {
    if (argc < 1) return ang_nil();
    RpcServer* srv = (RpcServer*)ang_api->native_instance_data(args[0]);
    if (srv && srv->listen_fd >= 0) {
        close(srv->listen_fd);
        srv->listen_fd = -1;
    }
    return ang_nil();
}


AngaraObject Angara_rpc_call(int argc, AngaraObject args[]) {
    if (argc < 3) {
        ang_api->throw_error("rpc.call(host, port, method, params?) needs at least 3 args.");
        return ang_nil();
    }
    if (!IS_STR(args[0]) || !ang_is_i64(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("rpc.call(host: string, port: i64, method: string, params?: any).");
        return ang_nil();
    }

    const char* host = ang_api->as_cstr(args[0]);
    int port = (int)ang_as_i64(args[1]);
    const char* method = ang_api->as_cstr(args[2]);

    JsonHandle req = json_bridge_new_object();
    json_bridge_object_add(req, "jsonrpc", json_bridge_new_string("2.0"));
    json_bridge_object_add(req, "method", json_bridge_new_string(method));

    static int64_t next_id = 1;
    int64_t my_id = next_id++;
    json_bridge_object_add(req, "id", json_bridge_new_number((double)my_id));

    if (argc >= 4) {
        JsonHandle params_json = angara_to_json(args[3]);
        json_bridge_object_add(req, "params", params_json);
        json_bridge_free(params_json);
    } else {
        json_bridge_object_add(req, "params", json_bridge_new_array());
    }

    const char* req_str = json_bridge_stringify(req);
    json_bridge_free(req);

    if (!req_str) {
        ang_api->throw_error("rpc.call: failed to serialize request.");
        return ang_nil();
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        json_bridge_free_string((char*)req_str);
        ang_api->throw_error("rpc.call: failed to create socket.");
        return ang_nil();
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        if (strcmp(host, "localhost") == 0) {
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        } else {
            close(sock);
            json_bridge_free_string((char*)req_str);
            ang_api->throw_error("rpc.call: invalid host address.");
            return ang_nil();
        }
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "rpc.call: failed to connect to %s:%d", host, port);
        close(sock);
        json_bridge_free_string((char*)req_str);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    size_t req_len = strlen(req_str);
    char* send_buf = (char*)malloc(req_len + 2);
    if (!send_buf) {
        close(sock);
        json_bridge_free_string((char*)req_str);
        ang_api->throw_error("rpc.call: out of memory.");
        return ang_nil();
    }
    memcpy(send_buf, req_str, req_len);
    send_buf[req_len] = '\n';
    send_buf[req_len + 1] = '\0';
    json_bridge_free_string((char*)req_str);

    if (send_all(sock, send_buf, req_len + 1) < 0) {
        free(send_buf);
        close(sock);
        ang_api->throw_error("rpc.call: failed to send request.");
        return ang_nil();
    }
    free(send_buf);

    ReadBuffer rbuf;
    rbuf.cap = 8192;
    rbuf.len = 0;
    rbuf.data = (char*)malloc(rbuf.cap);
    if (!rbuf.data) {
        close(sock);
        ang_api->throw_error("rpc.call: out of memory.");
        return ang_nil();
    }

    while (1) {
        char c;
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') break;
        if (rbuf.len + 1 >= rbuf.cap) {
            rbuf.cap *= 2;
            char* new_data = (char*)realloc(rbuf.data, rbuf.cap);
            if (!new_data) { free(rbuf.data); close(sock); return ang_nil(); }
            rbuf.data = new_data;
        }
        rbuf.data[rbuf.len++] = c;
    }
    close(sock);

    if (rbuf.len == 0) {
        free(rbuf.data);
        ang_api->throw_error("rpc.call: empty response from server.");
        return ang_nil();
    }
    rbuf.data[rbuf.len] = '\0';

    char* error_msg = NULL;
    JsonHandle resp = json_bridge_parse(rbuf.data, &error_msg);
    free(rbuf.data);

    if (!resp) {
        if (error_msg) {
            ang_api->throw_error(error_msg);
            free(error_msg);
        } else {
            ang_api->throw_error("rpc.call: failed to parse response.");
        }
        return ang_nil();
    }

    AngaraObject rpc_result = ang_nil();
    bool found_result = false;
    bool found_error = false;

    if (json_bridge_is_object(resp)) {
        size_t sz = json_bridge_object_size(resp);
        for (size_t i = 0; i < sz; ++i) {
            const char* key = json_bridge_object_get_key_at(resp, i);
            JsonHandle val = json_bridge_object_get_value_at(resp, i);
            if (key) {
                if (strcmp(key, "result") == 0) {
                    rpc_result = json_to_angara(val);
                    found_result = true;
                } else if (strcmp(key, "error") == 0 && !json_bridge_is_null(val)) {
                    AngaraObject err_rec = json_to_angara(val);
                    const char* err_str = ang_api->as_cstr(ang_api->to_string(err_rec));
                    ang_api->throw_error(err_str ? err_str : "rpc.call: server returned an error.");
                    ang_api->decref(err_rec);
                    found_error = true;
                }
                free((void*)key);
            }
        }
    }

    json_bridge_free(resp);

    if (found_error) return ang_nil();
    if (!found_result) return ang_nil();

    return rpc_result;
}


AngaraObject Angara_rpc_notify(int argc, AngaraObject args[]) {
    if (argc < 3) {
        ang_api->throw_error("rpc.notify(host, port, method, params?) needs at least 3 args.");
        return ang_nil();
    }
    if (!IS_STR(args[0]) || !ang_is_i64(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("rpc.notify(host: string, port: i64, method: string, params?: any).");
        return ang_nil();
    }

    const char* host = ang_api->as_cstr(args[0]);
    int port = (int)ang_as_i64(args[1]);
    const char* method = ang_api->as_cstr(args[2]);

    JsonHandle req = json_bridge_new_object();
    json_bridge_object_add(req, "jsonrpc", json_bridge_new_string("2.0"));
    json_bridge_object_add(req, "method", json_bridge_new_string(method));

    if (argc >= 4) {
        JsonHandle params_json = angara_to_json(args[3]);
        json_bridge_object_add(req, "params", params_json);
        json_bridge_free(params_json);
    } else {
        json_bridge_object_add(req, "params", json_bridge_new_array());
    }

    const char* req_str = json_bridge_stringify(req);
    json_bridge_free(req);

    if (!req_str) return ang_nil();

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        json_bridge_free_string((char*)req_str);
        return ang_nil();
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        if (strcmp(host, "localhost") == 0) {
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        } else {
            close(sock);
            json_bridge_free_string((char*)req_str);
            return ang_nil();
        }
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        json_bridge_free_string((char*)req_str);
        return ang_nil();
    }

    size_t req_len = strlen(req_str);
    char* send_buf = (char*)malloc(req_len + 2);
    if (send_buf) {
        memcpy(send_buf, req_str, req_len);
        send_buf[req_len] = '\n';
        send_buf[req_len + 1] = '\0';
        send_all(sock, send_buf, req_len + 1);
        free(send_buf);
    }
    json_bridge_free_string((char*)req_str);
    close(sock);

    return ang_nil();
}


static const AngaraMethodDef SERVER_METHODS[] = {
    {"service",       (AngaraMethodFn)Angara_RpcServer_service,       "->n"},
    {"poll_clients",  (AngaraMethodFn)Angara_RpcServer_poll_clients,  "->l<i>"},
    {"read_request",  (AngaraMethodFn)Angara_RpcServer_read_request,  "i->{}?"},
    {"respond",       (AngaraMethodFn)Angara_RpcServer_respond,       "aai->n"},
    {"close",         (AngaraMethodFn)Angara_RpcServer_close,         "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef SERVER_CLASS_DEF = { "RpcServer", NULL, SERVER_METHODS };

static const AngaraFuncDef RPC_EXPORTS[] = {
    {"create_server", Angara_rpc_create_server, "i->RpcServer", &SERVER_CLASS_DEF},
    {"call",          Angara_rpc_call,           "sisl<a>?->a",  NULL},
    {"notify",        Angara_rpc_notify,         "sisl<a>?->n",  NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(rpc) {
    ang_api = api;
    *def_count = (sizeof(RPC_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return RPC_EXPORTS;
}
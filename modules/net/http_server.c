/// Angara HTTP server — raw-socket HTTP/1.1, callback-driven.
///
/// Callback mode:
///   let srv = http.server(8080)
///   srv.on_request(func(req) {
///       return {status = 200, body = "Hello " + req.path,
///               headers = {"Content-Type": "text/plain"}}
///   })
///   srv.run()
///
/// Poll mode (legacy):
///   for (req in srv.poll(100)) { srv.respond(req.id, 200, body) }
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
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

/* ---- HTTP request parser ---- */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} HttpBuffer;

typedef struct {
    int    fd;
    char*  method;
    char*  path;
    char*  body;
    size_t body_len;
    AngaraObject headers;   /* record<string, string> */
    int    complete;        /* 1 when full request received */
    int    keep_alive;
} HttpRequest;

typedef struct {
    int          listen_fd;
    HttpRequest** reqs;
    size_t       req_count;
    size_t       req_cap;
    AngaraObject on_req_cb;  /* callback for on_request() */
} HttpServer;

/* ---- internal ---- */

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void free_request(HttpRequest* req) {
    if (!req) return;
    if (req->fd >= 0) close(req->fd);
    free(req->method);
    free(req->path);
    free(req->body);
    ang_api->decref(req->headers);
    free(req);
}

/* poor-man's dynamic string */
typedef struct { char* data; size_t len; size_t cap; } DynStr;
static void ds_init(DynStr* ds) { ds->cap = 256; ds->len = 0; ds->data = (char*)malloc(ds->cap); }
static void ds_append_n(DynStr* ds, const char* s, size_t n) {
    if (ds->len + n >= ds->cap) { ds->cap = ds->len + n + 256; ds->data = (char*)realloc(ds->data, ds->cap); }
    memcpy(ds->data + ds->len, s, n);
    ds->len += n;
}
static char* ds_detach(DynStr* ds) {
    char* r = ds->data;
    r[ds->len] = '\0';
    ds->data = NULL; ds->len = ds->cap = 0;
    return r;
}
static void ds_free(DynStr* ds) { free(ds->data); (void)ds; }

/* parse HTTP request from fd.  Returns 1 when fully parsed, 0 if more data needed, -1 on error. */
static int parse_http_request(HttpRequest* req) {
    char buf[4096];
    ssize_t n = recv(req->fd, buf, sizeof(buf), 0);
    if (n <= 0) return -1;

    /* append to body buffer (we reuse body field for raw input during parsing) */
    size_t old_len = req->body_len;
    req->body_len += (size_t)n;
    req->body = (char*)realloc(req->body, req->body_len + 1);
    memcpy(req->body + old_len, buf, (size_t)n);
    req->body[req->body_len] = '\0';

    char* raw = req->body;
    size_t raw_len = req->body_len;

    /* find end of headers (\r\n\r\n) */
    char* header_end = strstr(raw, "\r\n\r\n");
    if (!header_end) {
        if (req->body_len > 65536) return -1;  /* too large, give up */
        return 0;  /* need more data */
    }

    size_t header_section_len = (size_t)(header_end - raw);
    (void)header_section_len;
    char* body_start = header_end + 4;
    size_t body_bytes = raw_len - (size_t)(body_start - raw);

    /* parse request line: METHOD SP PATH SP HTTP/1.x\r\n */
    char* line_end = strstr(raw, "\r\n");
    if (!line_end) return -1;
    *line_end = '\0';
    char* request_line = raw;

    /* METHOD */
    char* sp1 = strchr(request_line, ' ');
    if (!sp1) return -1;
    *sp1 = '\0';
    req->method = strdup(request_line);
    char* rest = sp1 + 1;

    /* PATH */
    char* sp2 = strchr(rest, ' ');
    if (!sp2) { free(req->method); req->method = NULL; return -1; }
    *sp2 = '\0';
    req->path = strdup(rest);
    /* HTTP version after sp2, ignored */

    /* parse headers */
    req->headers = ang_api->record_new();
    char* hdr_line = line_end + 2;
    while (hdr_line < header_end && *hdr_line != '\r') {
        char* hdr_end = strstr(hdr_line, "\r\n");
        if (!hdr_end) break;
        *hdr_end = '\0';

        char* colon = strchr(hdr_line, ':');
        if (colon) {
            *colon = '\0';
            char* key = hdr_line;
            char* val = colon + 1;
            while (*val == ' ') val++;  /* trim leading space */
            /* lowercase the key for case-insensitive lookup */
            for (char* p = key; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            ang_api->record_set(req->headers, key, ang_api->string(val));
        }
        hdr_line = hdr_end + 2;
    }

    /* check Content-Length */
    AngaraObject cl_val = ang_api->record_get(req->headers, "content-length");
    int64_t content_length = -1;
    if (ang_is_obj(cl_val)) {
        const char* s = ang_api->as_cstr(cl_val);
        content_length = (int64_t)atoll(s);
    }
    ang_api->decref(cl_val);

    /* check Connection: keep-alive */
    AngaraObject conn_val = ang_api->record_get(req->headers, "connection");
    req->keep_alive = 0;
    if (ang_is_obj(conn_val)) {
        const char* s = ang_api->as_cstr(conn_val);
        if (strcasecmp(s, "keep-alive") == 0) req->keep_alive = 1;
    }
    ang_api->decref(conn_val);

    if (content_length > 0) {
        if ((int64_t)body_bytes < content_length) {
            /* need more body data */
            /* restore header_end for next read */
            /* but we already modified the buffer — this is a simplification:
               we only support requests where body arrives with headers */
            return 0;
        }
        /* extract body */
        req->body = (char*)malloc((size_t)content_length + 1);
        memcpy(req->body, body_start, (size_t)content_length);
        req->body[content_length] = '\0';
        req->body_len = (size_t)content_length;
    } else {
        /* no body */
        free(req->body);
        req->body = NULL;
        req->body_len = 0;
    }

    req->complete = 1;
    return 1;
}


/* ---- public API ---- */

static void finalize_server(void* data) {
    HttpServer* srv = (HttpServer*)data;
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    for (size_t i = 0; i < srv->req_count; i++) free_request(srv->reqs[i]);
    free(srv->reqs);
    if (ang_is_obj(srv->on_req_cb)) ang_api->decref(srv->on_req_cb);
    free(srv);
}

AngaraObject Angara_http_server_server(int arg_count, AngaraObject* args) {
    int port = 8080;
    if (arg_count >= 1 && ang_is_i64(args[0])) port = (int)ang_as_i64(args[0]);

    int sock = socket(AF_INET6, SOCK_STREAM, 0);
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_STREAM, 0);  /* fallback IPv4 */
    }
    if (sock < 0) {
        ang_api->throw_error("http.server: failed to create socket.");
        return ang_nil();
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    /* dual-stack: accept IPv4 on IPv6 socket */
    {
        int v6only = 0;
        setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons((uint16_t)port);
    addr.sin6_addr = in6addr_any;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        /* try IPv4 */
        struct sockaddr_in addr4;
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons((uint16_t)port);
        addr4.sin_addr.s_addr = INADDR_ANY;
        close(sock);
        sock = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (sock < 0 || bind(sock, (struct sockaddr*)&addr4, sizeof(addr4)) < 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "http.server: bind failed on port %d: %s", port, strerror(errno));
            if (sock >= 0) close(sock);
            ang_api->throw_error(buf);
            return ang_nil();
        }
    }

    if (listen(sock, 128) < 0) {
        close(sock);
        ang_api->throw_error("http.server: listen failed.");
        return ang_nil();
    }

    set_nonblocking(sock);

    HttpServer* srv = (HttpServer*)calloc(1, sizeof(HttpServer));
    srv->listen_fd = sock;
    srv->req_cap = 16;
    srv->reqs = (HttpRequest**)calloc(srv->req_cap, sizeof(HttpRequest*));

    return ang_api->native_instance_new(srv, finalize_server, "HttpServer");
}

AngaraObject Angara_HttpServer_on_request(int arg_count, AngaraObject* args) {
    /* srv.on_request(callback) — callback receives {id, method, path, headers, body?}
       and must return {status?, body?, headers?} or a plain string. */
    if (arg_count < 2) {
        ang_api->throw_error("http.on_request(callback) expects a closure.");
        return ang_nil();
    }
    HttpServer* srv = (HttpServer*)ang_api->native_instance_data(args[0]);
    if (!srv) return ang_nil();

    if (ang_is_obj(srv->on_req_cb)) ang_api->decref(srv->on_req_cb);
    srv->on_req_cb = args[1];
    ang_api->incref(srv->on_req_cb);
    return ang_nil();
}

AngaraObject Angara_HttpServer_run(int arg_count, AngaraObject* args) {
    /* srv.run() — blocks, accepts connections, parses requests,
       calls on_request callback, sends response. */
    (void)arg_count; (void)args;
    HttpServer* srv = (HttpServer*)ang_api->native_instance_data(args[0]);
    if (!srv || srv->listen_fd < 0) return ang_nil();

    if (!ang_is_obj(srv->on_req_cb)) {
        ang_api->throw_error("http.run: no callback set. Call on_request() first.");
        return ang_nil();
    }

    while (1) {
        /* accept */
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(srv->listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);  /* 1ms */
                continue;
            }
            break;
        }
        set_nonblocking(client_fd);

        HttpRequest* req = (HttpRequest*)calloc(1, sizeof(HttpRequest));
        req->fd = client_fd;
        req->body = NULL;
        req->body_len = 0;

        /* read */
        int rc = parse_http_request(req);
        if (rc != 1) {
            free_request(req);
            continue;
        }

        /* build request record for callback */
        AngaraObject req_rec = ang_api->record_new();
        ang_api->record_set(req_rec, "id",      ang_i64((int64_t)(intptr_t)req));
        ang_api->record_set(req_rec, "method",  ang_api->string(req->method));
        ang_api->record_set(req_rec, "path",    ang_api->string(req->path));
        ang_api->record_set(req_rec, "headers", req->headers);
        if (req->body)
            ang_api->record_set(req_rec, "body", ang_api->string_len(req->body, req->body_len));

        /* call the Angara callback */
        AngaraObject cb_args[1] = { req_rec };
        AngaraObject cb_result = ang_api->call(srv->on_req_cb, 1, cb_args);

        /* interpret the callback result */
        int status = 200;
        const char* body = "";
        size_t body_len = 0;
        AngaraObject headers_obj = ang_nil();

        if (IS_STR(cb_result)) {
            body = ang_api->as_cstr(cb_result);
            body_len = ang_api->str_len(cb_result);
        } else if (IS_REC(cb_result)) {
            AngaraObject s = ang_api->record_get(cb_result, "status");
            if (ang_is_i64(s)) status = (int)ang_as_i64(s);
            ang_api->decref(s);

            AngaraObject b = ang_api->record_get(cb_result, "body");
            if (IS_STR(b)) {
                body = ang_api->as_cstr(b);
                body_len = ang_api->str_len(b);
            }
            ang_api->decref(b);

            AngaraObject h = ang_api->record_get(cb_result, "headers");
            if (IS_REC(h)) headers_obj = h;
            else ang_api->decref(h);
        }

        /* build and send response */
        const char* status_text = "OK";
        switch (status) {
            case 200: status_text = "OK"; break;
            case 201: status_text = "Created"; break;
            case 204: status_text = "No Content"; break;
            case 400: status_text = "Bad Request"; break;
            case 404: status_text = "Not Found"; break;
            case 500: status_text = "Internal Server Error"; break;
            default:  status_text = "Unknown"; break;
        }

        char header_buf[4096];
        int hl = snprintf(header_buf, sizeof(header_buf),
                          "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n", status, status_text, body_len);

        /* user headers */
        if (IS_REC(headers_obj)) {
            size_t hc = ang_api->record_len(headers_obj);
            for (size_t i = 0; i < hc; i++) {
                const char* key = ang_api->record_key_at(headers_obj, i);
                AngaraObject val = ang_api->record_val_at(headers_obj, i);
                if (key && IS_STR(val)) {
                    hl += snprintf(header_buf + hl, sizeof(header_buf) - hl,
                                   "%s: %s\r\n", key, ang_api->as_cstr(val));
                }
                ang_api->decref(val);
            }
        }
        hl += snprintf(header_buf + hl, sizeof(header_buf) - hl, "\r\n");

        send(req->fd, header_buf, (size_t)hl, MSG_NOSIGNAL);
        if (body_len > 0) send(req->fd, body, body_len, MSG_NOSIGNAL);

        /* cleanup */
        ang_api->decref(req_rec);
        ang_api->decref(cb_result);
        if (IS_REC(headers_obj)) ang_api->decref(headers_obj);
        free_request(req);
    }

    return ang_nil();
}

AngaraObject Angara_HttpServer_poll(int arg_count, AngaraObject* args) {
    HttpServer* srv = (HttpServer*)ang_api->native_instance_data(args[0]);
    if (!srv || srv->listen_fd < 0) return ang_api->list_new();

    int timeout_ms = 0;
    if (arg_count >= 2 && ang_is_i64(args[1])) timeout_ms = (int)ang_as_i64(args[1]);

    /* accept new connections */
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(srv->listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) break;
        set_nonblocking(client_fd);

        HttpRequest* req = (HttpRequest*)calloc(1, sizeof(HttpRequest));
        req->fd = client_fd;
        req->body = NULL;
        req->body_len = 0;

        if (srv->req_count >= srv->req_cap) {
            srv->req_cap *= 2;
            srv->reqs = (HttpRequest**)realloc(srv->reqs, srv->req_cap * sizeof(HttpRequest*));
        }
        srv->reqs[srv->req_count++] = req;
    }

    /* poll existing connections for data */
    AngaraObject result = ang_api->list_new();

    for (size_t i = 0; i < srv->req_count; ) {
        HttpRequest* req = srv->reqs[i];

        if (!req->complete) {
            struct pollfd pfd;
            pfd.fd = req->fd;
            pfd.events = POLLIN;
            int pret = poll(&pfd, 1, timeout_ms > 0 ? timeout_ms : 0);
            if (pret > 0) {
                int rc = parse_http_request(req);
                if (rc < 0) {
                    /* parse error or disconnect — drop */
                    free_request(req);
                    srv->reqs[i] = srv->reqs[--srv->req_count];
                    continue;
                }
            }
        }

        if (req->complete) {
            AngaraObject rec = ang_api->record_new();
            ang_api->record_set(rec, "id",      ang_i64((int64_t)(intptr_t)req));
            ang_api->record_set(rec, "method",  ang_api->string(req->method));
            ang_api->record_set(rec, "path",    ang_api->string(req->path));
            ang_api->record_set(rec, "headers", req->headers);
            if (req->body) {
                ang_api->record_set(rec, "body", ang_api->string_len(req->body, req->body_len));
            }
            ang_api->list_push(result, rec);
            ang_api->decref(rec);
        }
        i++;
    }

    return result;
}

AngaraObject Angara_HttpServer_respond(int arg_count, AngaraObject* args) {
    /* srv.respond(req_id, status, body, headers?) */
    if (arg_count < 4) {
        ang_api->throw_error("http.respond(req_id, status, body, headers?) expects at least 3 args.");
        return ang_nil();
    }

    HttpServer* srv = (HttpServer*)ang_api->native_instance_data(args[0]);
    if (!srv) return ang_nil();

    HttpRequest* req = (HttpRequest*)(intptr_t)ang_as_i64(args[1]);
    if (!req) return ang_nil();

    int status = (int)ang_as_i64(args[2]);
    const char* status_text = "OK";
    switch (status) {
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 204: status_text = "No Content"; break;
        case 301: status_text = "Moved Permanently"; break;
        case 302: status_text = "Found"; break;
        case 400: status_text = "Bad Request"; break;
        case 401: status_text = "Unauthorized"; break;
        case 403: status_text = "Forbidden"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        default:  status_text = "Unknown"; break;
    }

    const char* body = "";
    size_t body_len = 0;
    if (IS_STR(args[3])) {
        body = ang_api->as_cstr(args[3]);
        body_len = ang_api->str_len(args[3]);
    }

    /* build response */
    DynStr resp;
    ds_init(&resp);

    char status_line[64];
    int sl_len = snprintf(status_line, sizeof(status_line), "HTTP/1.1 %d %s\r\n", status, status_text);
    ds_append_n(&resp, status_line, (size_t)sl_len);

    /* Content-Length */
    char cl_buf[64];
    int cl_len = snprintf(cl_buf, sizeof(cl_buf), "Content-Length: %zu\r\n", body_len);
    ds_append_n(&resp, cl_buf, (size_t)cl_len);

    /* user headers */
    if (arg_count >= 5 && IS_REC(args[4])) {
        size_t hdr_count = ang_api->record_len(args[4]);
        for (size_t i = 0; i < hdr_count; i++) {
            const char* key = ang_api->record_key_at(args[4], i);
            AngaraObject val = ang_api->record_val_at(args[4], i);
            if (key && IS_STR(val)) {
                ds_append_n(&resp, key, strlen(key));
                ds_append_n(&resp, ": ", 2);
                const char* vs = ang_api->as_cstr(val);
                ds_append_n(&resp, vs, ang_api->str_len(val));
                ds_append_n(&resp, "\r\n", 2);
            }
            ang_api->decref(val);
        }
    } else {
        ds_append_n(&resp, "Content-Type: text/plain\r\n", 26);
    }

    ds_append_n(&resp, "\r\n", 2);

    /* body */
    if (body_len > 0) ds_append_n(&resp, body, body_len);

    char* resp_buf = ds_detach(&resp);
    size_t resp_len = resp.len;

    /* send */
    send(req->fd, resp_buf, resp_len, MSG_NOSIGNAL);
    free(resp_buf);

    /* close the connection (HTTP/1.0 style unless keep-alive) */
    if (!req->keep_alive || status >= 400) {
        close(req->fd);
        req->fd = -1;
    }
    req->complete = 0;  /* ready for next request on keep-alive */

    /* free request data */
    free(req->method); req->method = NULL;
    free(req->path);   req->path = NULL;
    free(req->body);   req->body = NULL;
    ang_api->decref(req->headers);
    req->headers = ang_api->record_new();
    req->body_len = 0;

    return ang_nil();
}

AngaraObject Angara_HttpServer_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    HttpServer* srv = (HttpServer*)ang_api->native_instance_data(args[0]);
    if (srv) {
        if (srv->listen_fd >= 0) { close(srv->listen_fd); srv->listen_fd = -1; }
        for (size_t i = 0; i < srv->req_count; i++) free_request(srv->reqs[i]);
        free(srv->reqs);
        srv->reqs = NULL;
        srv->req_count = 0;
        srv->req_cap = 0;
    }
    return ang_nil();
}


/* ---- export table ---- */

static const AngaraMethodDef SERVER_METHODS[] = {
    {"on_request", (AngaraMethodFn)Angara_HttpServer_on_request, "a->n"},
    {"run",        (AngaraMethodFn)Angara_HttpServer_run,        "->n"},
    {"poll",       (AngaraMethodFn)Angara_HttpServer_poll,       "i?->l<{}>"},
    {"respond",    (AngaraMethodFn)Angara_HttpServer_respond,    "iis{}?->n"},
    {"close",      (AngaraMethodFn)Angara_HttpServer_close,      "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef SERVER_CLASS = { "HttpServer", NULL, SERVER_METHODS };

static const AngaraFuncDef HTTP_EXPORTS[] = {
    {"server", Angara_http_server_server, "i?->HttpServer", &SERVER_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(http_server) {
    ang_api = api;
    *def_count = (sizeof(HTTP_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return HTTP_EXPORTS;
}

/// Angara TLS module — OpenSSL-wrapped secure sockets.
///
///   let conn = tls.connect("example.com", 443)
///   conn.send("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n")
///   let response = conn.recv(4096)
///   conn.close()
///
///   let listener = tls.listen(443, "cert.pem", "key.pem")
///   let client = listener.accept()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <pthread.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

static pthread_once_t ssl_once = PTHREAD_ONCE_INIT;

static void init_ssl(void) {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

/* ---- TlsConn (client) ---- */

typedef struct {
    int    fd;
    SSL*   ssl;
    SSL_CTX* ctx;
} TlsConn;

static void finalize_tls_conn(void* data) {
    TlsConn* c = (TlsConn*)data;
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    if (c->ctx) SSL_CTX_free(c->ctx);
    if (c->fd >= 0) close(c->fd);
    free(c);
}

AngaraObject Angara_tls_connect(int arg_count, AngaraObject* args) {
    pthread_once(&ssl_once, init_ssl);

    const char* host = ang_api->as_cstr(args[0]);
    int port = (int)ang_as_i64(args[1]);

    AngaraObject opts = (arg_count >= 3 && IS_REC(args[2])) ? args[2] : ang_nil();

    /* resolve host */
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        ang_api->throw_error("tls.connect: DNS resolution failed.");
        return ang_nil();
    }

    int fd = -1;
    struct addrinfo* rp;
    for (rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0) {
        ang_api->throw_error("tls.connect: connection failed.");
        return ang_nil();
    }

    /* create SSL context */
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); ang_api->throw_error("tls.connect: SSL_CTX_new failed."); return ang_nil(); }

    /* apply options */
    if (IS_REC(opts)) {
        AngaraObject v = ang_api->record_get(opts, "verify");
        if (ang_is_bool(v) && !ang_as_bool(v))
            SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
        else
            SSL_CTX_set_default_verify_paths(ctx);
        ang_api->decref(v);

        v = ang_api->record_get(opts, "ca_bundle");
        if (IS_STR(v)) SSL_CTX_load_verify_locations(ctx, ang_api->as_cstr(v), NULL);
        ang_api->decref(v);

        v = ang_api->record_get(opts, "client_cert");
        if (IS_STR(v)) SSL_CTX_use_certificate_file(ctx, ang_api->as_cstr(v), SSL_FILETYPE_PEM);
        ang_api->decref(v);

        v = ang_api->record_get(opts, "client_key");
        if (IS_STR(v)) SSL_CTX_use_PrivateKey_file(ctx, ang_api->as_cstr(v), SSL_FILETYPE_PEM);
        ang_api->decref(v);
    } else {
        SSL_CTX_set_default_verify_paths(ctx);
    }

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);  /* SNI */

    if (SSL_connect(ssl) != 1) {
        char buf[256];
        unsigned long err = ERR_get_error();
        snprintf(buf, sizeof(buf), "tls.connect: SSL_connect failed: %s",
                 ERR_reason_error_string(err));
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    TlsConn* c = (TlsConn*)calloc(1, sizeof(TlsConn));
    c->fd = fd;
    c->ssl = ssl;
    c->ctx = ctx;
    return ang_api->native_instance_new(c, finalize_tls_conn, "TlsConn");
}

AngaraObject Angara_TlsConn_send(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) return ang_i64(-1);
    TlsConn* c = (TlsConn*)ang_api->native_instance_data(args[0]);
    if (!c || !c->ssl) return ang_i64(-1);
    const char* data = ang_api->as_cstr(args[1]);
    size_t len = ang_api->str_len(args[1]);
    int sent = SSL_write(c->ssl, data, (int)len);
    return ang_i64(sent);
}

AngaraObject Angara_TlsConn_recv(int arg_count, AngaraObject* args) {
    size_t buf_size = 4096;
    if (arg_count >= 2 && ang_is_i64(args[1])) buf_size = (size_t)ang_as_i64(args[1]);
    if (buf_size == 0) buf_size = 4096;
    TlsConn* c = (TlsConn*)ang_api->native_instance_data(args[0]);
    if (!c || !c->ssl) return ang_nil();
    char* buf = (char*)malloc(buf_size);
    int n = SSL_read(c->ssl, buf, (int)buf_size);
    if (n <= 0) { free(buf); return ang_nil(); }
    return ang_api->string_no_copy(buf, (size_t)n);
}

AngaraObject Angara_TlsConn_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TlsConn* c = (TlsConn*)ang_api->native_instance_data(args[0]);
    if (c) {
        if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = NULL; }
        if (c->ctx) { SSL_CTX_free(c->ctx); c->ctx = NULL; }
        if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    }
    return ang_nil();
}

AngaraObject Angara_TlsConn_fileno(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TlsConn* c = (TlsConn*)ang_api->native_instance_data(args[0]);
    return ang_i64(c ? c->fd : -1);
}


/* ---- TlsListener (server) ---- */

typedef struct {
    int      listen_fd;
    SSL_CTX* ctx;
} TlsListener;

static void finalize_tls_listener(void* data) {
    TlsListener* l = (TlsListener*)data;
    if (l->ctx) SSL_CTX_free(l->ctx);
    if (l->listen_fd >= 0) close(l->listen_fd);
    free(l);
}

AngaraObject Angara_tls_listen(int arg_count, AngaraObject* args) {
    pthread_once(&ssl_once, init_ssl);

    int port = (int)ang_as_i64(args[0]);
    const char* cert_file = ang_api->as_cstr(args[1]);
    const char* key_file  = ang_api->as_cstr(args[2]);

    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { ang_api->throw_error("tls.listen: SSL_CTX_new failed."); return ang_nil(); }

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
        char buf[256];
        snprintf(buf, sizeof(buf), "tls.listen: failed to load cert '%s'", cert_file);
        SSL_CTX_free(ctx);
        ang_api->throw_error(buf);
        return ang_nil();
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        char buf[256];
        snprintf(buf, sizeof(buf), "tls.listen: failed to load key '%s'", key_file);
        SSL_CTX_free(ctx);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) { fd = socket(AF_INET, SOCK_STREAM, 0); }
    if (fd < 0) { SSL_CTX_free(ctx); ang_api->throw_error("tls.listen: socket failed."); return ang_nil(); }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    { int v6only = 0; setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)); }

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons((uint16_t)port);
    addr.sin6_addr = in6addr_any;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        struct sockaddr_in addr4;
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons((uint16_t)port);
        addr4.sin_addr.s_addr = INADDR_ANY;
        close(fd);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (fd < 0 || bind(fd, (struct sockaddr*)&addr4, sizeof(addr4)) < 0) {
            SSL_CTX_free(ctx); if (fd >= 0) close(fd);
            ang_api->throw_error("tls.listen: bind failed.");
            return ang_nil();
        }
    }
    if (listen(fd, 128) < 0) { SSL_CTX_free(ctx); close(fd); ang_api->throw_error("tls.listen: listen failed."); return ang_nil(); }

    TlsListener* l = (TlsListener*)calloc(1, sizeof(TlsListener));
    l->listen_fd = fd;
    l->ctx = ctx;
    return ang_api->native_instance_new(l, finalize_tls_listener, "TlsListener");
}

AngaraObject Angara_TlsListener_accept(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TlsListener* l = (TlsListener*)ang_api->native_instance_data(args[0]);
    if (!l || l->listen_fd < 0) return ang_nil();

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int client_fd = accept(l->listen_fd, (struct sockaddr*)&addr, &addr_len);
    if (client_fd < 0) return ang_nil();

    SSL* ssl = SSL_new(l->ctx);
    SSL_set_fd(ssl, client_fd);
    if (SSL_accept(ssl) != 1) {
        SSL_free(ssl); close(client_fd);
        return ang_nil();
    }

    TlsConn* c = (TlsConn*)calloc(1, sizeof(TlsConn));
    c->fd = client_fd;
    c->ssl = ssl;
    c->ctx = NULL;  /* ctx owned by listener */
    return ang_api->native_instance_new(c, finalize_tls_conn, "TlsConn");
}

AngaraObject Angara_TlsListener_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TlsListener* l = (TlsListener*)ang_api->native_instance_data(args[0]);
    if (l) {
        if (l->listen_fd >= 0) { close(l->listen_fd); l->listen_fd = -1; }
        if (l->ctx) { SSL_CTX_free(l->ctx); l->ctx = NULL; }
    }
    return ang_nil();
}


static const AngaraMethodDef TLSCONN_METHODS[] = {
    {"send",   (AngaraMethodFn)Angara_TlsConn_send,   "si?->i"},
    {"recv",   (AngaraMethodFn)Angara_TlsConn_recv,   "i?->s?"},
    {"close",  (AngaraMethodFn)Angara_TlsConn_close,  "->n"},
    {"fileno", (AngaraMethodFn)Angara_TlsConn_fileno, "->i"},
    {NULL, NULL, NULL}
};
static const AngaraClassDef TLSCONN_CLASS = { "TlsConn", NULL, TLSCONN_METHODS };

static const AngaraMethodDef TLSLIST_METHODS[] = {
    {"accept", (AngaraMethodFn)Angara_TlsListener_accept, "->TlsConn?"},
    {"close",  (AngaraMethodFn)Angara_TlsListener_close,  "->n"},
    {NULL, NULL, NULL}
};
static const AngaraClassDef TLSLIST_CLASS = { "TlsListener", NULL, TLSLIST_METHODS };

static const AngaraFuncDef TLS_EXPORTS[] = {
    {"connect", Angara_tls_connect, "si{}?->TlsConn",     &TLSCONN_CLASS},
    {"listen",  Angara_tls_listen,  "iss->TlsListener",    &TLSLIST_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(tls) {
    ang_api = api;
    *def_count = (sizeof(TLS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return TLS_EXPORTS;
}

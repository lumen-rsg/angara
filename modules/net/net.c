#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

typedef struct {
    int fd;
    int connected;
} TcpConn;

static void finalize_tcp_conn(void* data) {
    TcpConn* conn = (TcpConn*)data;
    if (conn->fd >= 0) close(conn->fd);
    free(conn);
}

AngaraObject Angara_net_tcp_connect(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("tcp_connect(host, port, timeout_ms?) expects a string and an i64.");
        return ang_nil();
    }

    const char* host = ang_api->as_cstr(args[0]);
    int port = (int)ang_as_i64(args[1]);
    int timeout_ms = 5000;
    if (arg_count >= 3 && ang_is_i64(args[2])) timeout_ms = (int)ang_as_i64(args[2]);

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int gai = getaddrinfo(host, port_str, &hints, &result);
    if (gai != 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "tcp_connect: failed to resolve '%s': %s", host, gai_strerror(gai));
        ang_api->throw_error(buf);
        return ang_nil();
    }

    int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(result);
        ang_api->throw_error("tcp_connect: failed to create socket.");
        return ang_nil();
    }

    set_nonblocking(sock);

    int connect_res = connect(sock, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (connect_res < 0 && errno != EINPROGRESS) {
        close(sock);
        char buf[256];
        snprintf(buf, sizeof(buf), "tcp_connect: failed to connect to %s:%d", host, port);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    if (connect_res < 0) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        int pret = poll(&pfd, 1, timeout_ms);
        if (pret <= 0) {
            close(sock);
            ang_api->throw_error("tcp_connect: connection timed out.");
            return ang_nil();
        }
        int err = 0;
        socklen_t err_len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &err_len);
        if (err != 0) {
            close(sock);
            char buf[256];
            snprintf(buf, sizeof(buf), "tcp_connect: connection failed: %s", strerror(err));
            ang_api->throw_error(buf);
            return ang_nil();
        }
    }

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    TcpConn* conn = (TcpConn*)calloc(1, sizeof(TcpConn));
    conn->fd = sock;
    conn->connected = 1;

    return ang_api->native_instance_new(conn, finalize_tcp_conn, "TcpConn");
}

AngaraObject Angara_TcpConn_send(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("send(data) expects a string.");
        return ang_nil();
    }
    TcpConn* conn = (TcpConn*)ang_api->native_instance_data(args[0]);
    if (!conn || conn->fd < 0) return ang_i64(-1);

    const char* data = ang_api->as_cstr(args[1]);
    size_t len = ang_api->str_len(args[1]);
    ssize_t sent = send(conn->fd, data, len, 0);
    return ang_i64((int64_t)sent);
}

AngaraObject Angara_TcpConn_recv(int arg_count, AngaraObject* args) {
    size_t buf_size = 4096;
    if (arg_count >= 2 && ang_is_i64(args[1])) buf_size = (size_t)ang_as_i64(args[1]);
    if (buf_size == 0) buf_size = 4096;

    TcpConn* conn = (TcpConn*)ang_api->native_instance_data(args[0]);
    if (!conn || conn->fd < 0) return ang_nil();

    char* buf = (char*)malloc(buf_size);
    ssize_t n = recv(conn->fd, buf, buf_size, 0);
    if (n <= 0) { free(buf); return ang_nil(); }
    return ang_api->string_no_copy(buf, (size_t)n);
}

AngaraObject Angara_TcpConn_recv_line(int arg_count, AngaraObject* args) {
    TcpConn* conn = (TcpConn*)ang_api->native_instance_data(args[0]);
    if (!conn || conn->fd < 0) return ang_nil();

    int timeout_ms = 5000;
    if (arg_count >= 2 && ang_is_i64(args[1])) timeout_ms = (int)ang_as_i64(args[1]);

    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);

    while (1) {
        struct pollfd pfd;
        pfd.fd = conn->fd;
        pfd.events = POLLIN;
        int pret = poll(&pfd, 1, timeout_ms);
        if (pret <= 0) break;

        char c;
        ssize_t n = recv(conn->fd, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') break;
        if (len + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return ang_nil(); }
            buf = nb;
        }
        buf[len++] = c;
    }

    if (len == 0) { free(buf); return ang_nil(); }
    return ang_api->string_no_copy(buf, len);
}

AngaraObject Angara_TcpConn_set_timeout(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_i64(args[1])) return ang_nil();
    TcpConn* conn = (TcpConn*)ang_api->native_instance_data(args[0]);
    if (!conn || conn->fd < 0) return ang_nil();

    struct timeval tv;
    tv.tv_sec = (time_t)(ang_as_i64(args[1]) / 1000);
    tv.tv_usec = (suseconds_t)((ang_as_i64(args[1]) % 1000) * 1000);
    setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return ang_nil();
}

AngaraObject Angara_TcpConn_close(int arg_count, AngaraObject* args) {
    TcpConn* conn = (TcpConn*)ang_api->native_instance_data(args[0]);
    if (conn && conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
        conn->connected = 0;
    }
    return ang_nil();
}

AngaraObject Angara_TcpConn_is_connected(int arg_count, AngaraObject* args) {
    TcpConn* conn = (TcpConn*)ang_api->native_instance_data(args[0]);
    return ang_bool(conn && conn->connected && conn->fd >= 0);
}

AngaraObject Angara_TcpConn_fileno(int arg_count, AngaraObject* args) {
    TcpConn* conn = (TcpConn*)ang_api->native_instance_data(args[0]);
    return ang_i64(conn ? (int64_t)conn->fd : -1);
}

typedef struct {
    int listen_fd;
    uint16_t port;
} TcpListener;

static void finalize_tcp_listener(void* data) {
    TcpListener* lstn = (TcpListener*)data;
    if (lstn->listen_fd >= 0) close(lstn->listen_fd);
    free(lstn);
}

AngaraObject Angara_net_tcp_listen(int arg_count, AngaraObject* args) {
    const char* host = "0.0.0.0";
    int port = 8080;
    int backlog = 128;

    if (arg_count >= 1 && IS_STR(args[0])) host = ang_api->as_cstr(args[0]);
    if (arg_count >= 2 && ang_is_i64(args[1])) port = (int)ang_as_i64(args[1]);
    if (arg_count >= 3 && ang_is_i64(args[2])) backlog = (int)ang_as_i64(args[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ang_api->throw_error("tcp_listen: failed to create socket.");
        return ang_nil();
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "tcp_listen: bind failed on %s:%d: %s", host, port, strerror(errno));
        close(sock);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    if (listen(sock, backlog) < 0) {
        close(sock);
        ang_api->throw_error("tcp_listen: listen failed.");
        return ang_nil();
    }

    set_nonblocking(sock);

    TcpListener* lstn = (TcpListener*)calloc(1, sizeof(TcpListener));
    lstn->listen_fd = sock;
    lstn->port = (uint16_t)port;

    return ang_api->native_instance_new(lstn, finalize_tcp_listener, "TcpListener");
}

AngaraObject Angara_TcpListener_accept(int arg_count, AngaraObject* args) {
    TcpListener* lstn = (TcpListener*)ang_api->native_instance_data(args[0]);
    if (!lstn || lstn->listen_fd < 0) return ang_nil();

    int timeout_ms = 0;
    if (arg_count >= 2 && ang_is_i64(args[1])) timeout_ms = (int)ang_as_i64(args[1]);

    if (timeout_ms > 0) {
        struct pollfd pfd;
        pfd.fd = lstn->listen_fd;
        pfd.events = POLLIN;
        int pret = poll(&pfd, 1, timeout_ms);
        if (pret <= 0) return ang_nil();
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(lstn->listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) return ang_nil();

    int flag = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    TcpConn* conn = (TcpConn*)calloc(1, sizeof(TcpConn));
    conn->fd = client_fd;
    conn->connected = 1;

    return ang_api->native_instance_new(conn, finalize_tcp_conn, "TcpConn");
}

AngaraObject Angara_TcpListener_accept_raw(int arg_count, AngaraObject* args) {
    TcpListener* lstn = (TcpListener*)ang_api->native_instance_data(args[0]);
    if (!lstn || lstn->listen_fd < 0) return ang_nil();

    int timeout_ms = 0;
    if (arg_count >= 2 && ang_is_i64(args[1])) timeout_ms = (int)ang_as_i64(args[1]);

    if (timeout_ms > 0) {
        struct pollfd pfd;
        pfd.fd = lstn->listen_fd;
        pfd.events = POLLIN;
        int pret = poll(&pfd, 1, timeout_ms);
        if (pret <= 0) return ang_nil();
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(lstn->listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) return ang_nil();

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "fd", ang_i64(client_fd));
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
    ang_api->record_set(rec, "addr", ang_api->string(addr_str));
    ang_api->record_set(rec, "port", ang_i64(ntohs(client_addr.sin_port)));
    return rec;
}

AngaraObject Angara_TcpListener_port(int arg_count, AngaraObject* args) {
    TcpListener* lstn = (TcpListener*)ang_api->native_instance_data(args[0]);
    return ang_i64(lstn ? (int64_t)lstn->port : -1);
}

AngaraObject Angara_TcpListener_close(int arg_count, AngaraObject* args) {
    TcpListener* lstn = (TcpListener*)ang_api->native_instance_data(args[0]);
    if (lstn && lstn->listen_fd >= 0) {
        close(lstn->listen_fd);
        lstn->listen_fd = -1;
    }
    return ang_nil();
}

typedef struct {
    int fd;
} UdpSock;

static void finalize_udp_sock(void* data) {
    UdpSock* us = (UdpSock*)data;
    if (us->fd >= 0) close(us->fd);
    free(us);
}

AngaraObject Angara_net_udp_socket(int arg_count, AngaraObject* args) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ang_api->throw_error("udp_socket: failed to create socket.");
        return ang_nil();
    }

    if (arg_count >= 1 && ang_is_i64(args[0])) {
        int port = (int)ang_as_i64(args[0]);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons((uint16_t)port);
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            char buf[128];
            snprintf(buf, sizeof(buf), "udp_socket: bind failed on port %d", port);
            ang_api->throw_error(buf);
            return ang_nil();
        }
    }

    UdpSock* us = (UdpSock*)calloc(1, sizeof(UdpSock));
    us->fd = sock;

    return ang_api->native_instance_new(us, finalize_udp_sock, "UdpSocket");
}

AngaraObject Angara_UdpSocket_send_to(int arg_count, AngaraObject* args) {
    if (arg_count < 4 || !IS_STR(args[1]) || !IS_STR(args[2]) || !ang_is_i64(args[3])) {
        ang_api->throw_error("send_to(data, host, port) expects string, string, i64.");
        return ang_nil();
    }
    UdpSock* us = (UdpSock*)ang_api->native_instance_data(args[0]);
    if (!us || us->fd < 0) return ang_i64(-1);

    const char* data = ang_api->as_cstr(args[1]);
    size_t len = ang_api->str_len(args[1]);
    const char* host = ang_api->as_cstr(args[2]);
    int port = (int)ang_as_i64(args[3]);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);
        if (getaddrinfo(host, port_str, &hints, &result) != 0) return ang_i64(-1);
        memcpy(&addr, result->ai_addr, sizeof(struct sockaddr_in));
        freeaddrinfo(result);
    }

    ssize_t sent = sendto(us->fd, data, len, 0, (struct sockaddr*)&addr, sizeof(addr));
    return ang_i64((int64_t)sent);
}

AngaraObject Angara_UdpSocket_recv_from(int arg_count, AngaraObject* args) {
    UdpSock* us = (UdpSock*)ang_api->native_instance_data(args[0]);
    if (!us || us->fd < 0) return ang_nil();

    size_t buf_size = 65536;
    if (arg_count >= 2 && ang_is_i64(args[1])) buf_size = (size_t)ang_as_i64(args[1]);
    if (buf_size == 0) buf_size = 65536;

    char* buf = (char*)malloc(buf_size);
    struct sockaddr_in sender;
    socklen_t slen = sizeof(sender);
    ssize_t n = recvfrom(us->fd, buf, buf_size, 0, (struct sockaddr*)&sender, &slen);
    if (n <= 0) { free(buf); return ang_nil(); }

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "data", ang_api->string_no_copy(buf, (size_t)n));
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender.sin_addr, addr_str, sizeof(addr_str));
    ang_api->record_set(rec, "addr", ang_api->string(addr_str));
    ang_api->record_set(rec, "port", ang_i64(ntohs(sender.sin_port)));
    return rec;
}

AngaraObject Angara_UdpSocket_set_broadcast(int arg_count, AngaraObject* args) {
    UdpSock* us = (UdpSock*)ang_api->native_instance_data(args[0]);
    if (!us || us->fd < 0) return ang_nil();
    if (arg_count >= 2 && ang_is_bool(args[1])) {
        int broadcast = ang_as_bool(args[1]) ? 1 : 0;
        setsockopt(us->fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    }
    return ang_nil();
}

AngaraObject Angara_UdpSocket_close(int arg_count, AngaraObject* args) {
    UdpSock* us = (UdpSock*)ang_api->native_instance_data(args[0]);
    if (us && us->fd >= 0) { close(us->fd); us->fd = -1; }
    return ang_nil();
}

AngaraObject Angara_net_resolve(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("resolve(hostname) expects a string.");
        return ang_nil();
    }

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(ang_api->as_cstr(args[0]), NULL, &hints, &result);
    if (gai != 0) return ang_api->list_new();

    AngaraObject list = ang_api->list_new();
    for (struct addrinfo* rp = result; rp; rp = rp->ai_next) {
        struct sockaddr_in* sin = (struct sockaddr_in*)rp->ai_addr;
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
        ang_api->list_push(list, ang_api->string(addr_str));
    }
    freeaddrinfo(result);
    return list;
}

AngaraObject Angara_net_reverse_lookup(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) return ang_api->string("");
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    if (inet_pton(AF_INET, ang_api->as_cstr(args[0]), &sa.sin_addr) <= 0) {
        return ang_api->string("");
    }
    char host[NI_MAXHOST];
    if (getnameinfo((struct sockaddr*)&sa, sizeof(sa), host, sizeof(host), NULL, 0, 0) != 0) {
        return ang_api->string("");
    }
    return ang_api->string(host);
}

AngaraObject Angara_net_parse_addr(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) return ang_nil();
    const char* addr = ang_api->as_cstr(args[0]);

    const char* colon = strrchr(addr, ':');
    if (!colon) return ang_nil();

    AngaraObject rec = ang_api->record_new();
    size_t host_len = (size_t)(colon - addr);
    char* host = (char*)malloc(host_len + 1);
    memcpy(host, addr, host_len);
    host[host_len] = '\0';

    if (host_len > 2 && host[0] == '[' && host[host_len - 1] == ']') {
        host[host_len - 1] = '\0';
        ang_api->record_set(rec, "host", ang_api->string(host + 1));
    } else {
        ang_api->record_set(rec, "host", ang_api->string(host));
    }
    free(host);

    int port = atoi(colon + 1);
    ang_api->record_set(rec, "port", ang_i64(port));
    return rec;
}

static const AngaraMethodDef TCPCONN_METHODS[] = {
    {"send",          (AngaraMethodFn)Angara_TcpConn_send,          "s->i"},
    {"recv",          (AngaraMethodFn)Angara_TcpConn_recv,          "i?->s?"},
    {"recv_line",     (AngaraMethodFn)Angara_TcpConn_recv_line,     "i?->s?"},
    {"set_timeout",   (AngaraMethodFn)Angara_TcpConn_set_timeout,   "i->n"},
    {"close",         (AngaraMethodFn)Angara_TcpConn_close,         "->n"},
    {"is_connected",  (AngaraMethodFn)Angara_TcpConn_is_connected,  "->b"},
    {"fileno",        (AngaraMethodFn)Angara_TcpConn_fileno,        "->i"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef TCPCONN_CLASS = { "TcpConn", NULL, TCPCONN_METHODS };

static const AngaraMethodDef LISTENER_METHODS[] = {
    {"accept",      (AngaraMethodFn)Angara_TcpListener_accept,      "i?->TcpConn?"},
    {"accept_raw",  (AngaraMethodFn)Angara_TcpListener_accept_raw,  "i?->{}?"},
    {"port",        (AngaraMethodFn)Angara_TcpListener_port,        "->i"},
    {"close",       (AngaraMethodFn)Angara_TcpListener_close,       "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef LISTENER_CLASS = { "TcpListener", NULL, LISTENER_METHODS };

static const AngaraMethodDef UDP_METHODS[] = {
    {"send_to",       (AngaraMethodFn)Angara_UdpSocket_send_to,       "ssi->i"},
    {"recv_from",     (AngaraMethodFn)Angara_UdpSocket_recv_from,     "i?->{}?"},
    {"set_broadcast", (AngaraMethodFn)Angara_UdpSocket_set_broadcast, "b->n"},
    {"close",         (AngaraMethodFn)Angara_UdpSocket_close,         "->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef UDP_CLASS = { "UdpSocket", NULL, UDP_METHODS };

static const AngaraFuncDef NET_EXPORTS[] = {
    {"tcp_connect",     Angara_net_tcp_connect,     "sii?->TcpConn",     &TCPCONN_CLASS},
    {"tcp_listen",      Angara_net_tcp_listen,      "s?ii?->TcpListener", &LISTENER_CLASS},
    {"udp_socket",      Angara_net_udp_socket,      "i?->UdpSocket",     &UDP_CLASS},
    {"resolve",         Angara_net_resolve,          "s->l<s>",           NULL},
    {"reverse_lookup",  Angara_net_reverse_lookup,   "s->s",             NULL},
    {"parse_addr",      Angara_net_parse_addr,       "s->{}?",           NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(net) {
    ang_api = api;
    *def_count = (sizeof(NET_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return NET_EXPORTS;
}
#include "../runtime/angara_runtime.h"
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

#define DEBUG_WS 1

void dbg(const char* func, const char* msg) {
    if (DEBUG_WS) fprintf(stderr, "[WS DEBUG] %s: %s\n", func, msg);
}

// --- Helpers ---
static void generate_id(char* buffer, size_t size) {
    snprintf(buffer, size, "%lx-%p", (unsigned long)time(NULL), buffer);
}

// --- Concurrent Queue ---
typedef struct QueueNode {
    void* data;
    struct QueueNode* next;
} QueueNode;

typedef struct {
    QueueNode* head;
    QueueNode* tail;
    pthread_mutex_t mutex;
} ConcurrentQueue;

void queue_init(ConcurrentQueue* q) {
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mutex, NULL);
}

void queue_push(ConcurrentQueue* q, void* data) {
    // dbg("queue", "Pushing item");
    QueueNode* node = (QueueNode*)malloc(sizeof(QueueNode));
    node->data = data;
    node->next = NULL;
    pthread_mutex_lock(&q->mutex);
    if (q->tail) { q->tail->next = node; q->tail = node; }
    else { q->head = q->tail = node; }
    pthread_mutex_unlock(&q->mutex);
}

void* queue_pop(ConcurrentQueue* q) {
    pthread_mutex_lock(&q->mutex);
    if (!q->head) { pthread_mutex_unlock(&q->mutex); return NULL; }
    // dbg("queue", "Popping item");
    QueueNode* node = q->head;
    void* data = node->data;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    pthread_mutex_unlock(&q->mutex);
    free(node);
    return data;
}

void queue_free_all(ConcurrentQueue* q, void (*free_fn)(void*)) {
    pthread_mutex_lock(&q->mutex);
    QueueNode* current = q->head;
    while (current) {
        QueueNode* next = current->next;
        if (free_fn) free_fn(current->data);
        free(current);
        current = next;
    }
    q->head = q->tail = NULL;
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
}

// --- Data Structures ---

typedef enum { NATIVE_TYPE_CLIENT = 100, NATIVE_TYPE_SERVER_CONNECTION = 200 } NativeObjectType;
typedef struct { NativeObjectType type; } NativeObjectHeader;

typedef struct msg_buffer {
    void *payload; size_t len; struct msg_buffer *next;
} msg_buffer;

// 1. Server State
typedef struct AngaraLwsServer {
    AngaraObject self_obj;
    struct lws_context *context;
    ConcurrentQueue accept_queue;
    struct lws_protocols* protocols;
} AngaraLwsServer;

// 2. Persistent Session State (Owned by Angara GC)
typedef struct AngaraLwsSession {
    NativeObjectHeader header;
    struct lws *wsi;
    bool is_connected;
    char id[64];
    ConcurrentQueue incoming_queue;
    msg_buffer *send_queue_head;
    pthread_mutex_t send_queue_mutex;
} AngaraLwsSession;

// 3. Ephemeral LWS State (Owned by LWS)
typedef struct ServerPerSessionData {
    AngaraLwsSession* session;
} ServerPerSessionData;

// 4. Client State
typedef struct AngaraLwsClient {
    NativeObjectHeader header;
    struct lws_context *context;
    struct lws *wsi;
    bool is_connected;
    char id[64];
    ConcurrentQueue incoming_queue;
    msg_buffer *send_queue_head;
    pthread_mutex_t send_queue_mutex;
    AngaraObject self_obj;
    struct lws_protocols* protocols;
} AngaraLwsClient;

// --- Finalizers ---

void finalize_server(void* data) {
    dbg("finalize_server", "Destroying server");
    AngaraLwsServer* server = (AngaraLwsServer*)data;
    if (server->context) lws_context_destroy(server->context);
    if (server->protocols) free(server->protocols);

    void* pending;
    while((pending = queue_pop(&server->accept_queue))) {
        AngaraObject* obj_ptr = (AngaraObject*)pending;
        angara_decref(*obj_ptr); free(obj_ptr);
    }
    queue_free_all(&server->accept_queue, NULL);
    free(server);
}

void finalize_client(void* data) {
    dbg("finalize_client", "Destroying client");
    AngaraLwsClient* client = (AngaraLwsClient*)data;
    if (client->context) lws_context_destroy(client->context);
    if (client->protocols) free(client->protocols);

    msg_buffer *current = client->send_queue_head;
    while(current) { msg_buffer* n = current->next; free(current->payload); free(current); current = n; }
    queue_free_all(&client->incoming_queue, free);
    pthread_mutex_destroy(&client->send_queue_mutex);
    free(client);
}

void finalize_server_session(void* data) {
    dbg("finalize_server_session", "Destroying session object (GC)");
    AngaraLwsSession* s = (AngaraLwsSession*)data;
    msg_buffer *current = s->send_queue_head;
    while(current) { msg_buffer* n = current->next; free(current->payload); free(current); current = n; }
    queue_free_all(&s->incoming_queue, free);
    pthread_mutex_destroy(&s->send_queue_mutex);
    free(s);
}

// --- Callback ---

static int angara_lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    void* context_user_data = lws_context_user(lws_get_context(wsi));

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            dbg("LWS", "LWS_CALLBACK_ESTABLISHED");
            ServerPerSessionData *psd = (ServerPerSessionData *)user;
            AngaraLwsServer *server = (AngaraLwsServer *)context_user_data;

            AngaraLwsSession* session = (AngaraLwsSession*)calloc(1, sizeof(AngaraLwsSession));
            session->header.type = NATIVE_TYPE_SERVER_CONNECTION;
            session->wsi = wsi;
            session->is_connected = true;
            generate_id(session->id, sizeof(session->id));
            queue_init(&session->incoming_queue);
            pthread_mutex_init(&session->send_queue_mutex, NULL);

            psd->session = session;

                AngaraObject wrapper = angara_create_native_instance(session, finalize_server_session, "WebSocket");
            angara_incref(wrapper); // Keep alive for queue

            AngaraObject* q_obj = (AngaraObject*)malloc(sizeof(AngaraObject));
            *q_obj = wrapper;
            queue_push(&server->accept_queue, q_obj);
            dbg("LWS", "Session queued");
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            ServerPerSessionData *psd = (ServerPerSessionData *)user;
            if (!psd || !psd->session) break;
            AngaraLwsSession* s = psd->session;

            pthread_mutex_lock(&s->send_queue_mutex);
            if (!s->send_queue_head) {
                pthread_mutex_unlock(&s->send_queue_mutex);
                break;
            }
            msg_buffer* msg = s->send_queue_head;
            s->send_queue_head = msg->next;
            pthread_mutex_unlock(&s->send_queue_mutex);

            lws_write(wsi, ((unsigned char*)msg->payload) + LWS_PRE, msg->len, LWS_WRITE_TEXT);
            free(msg->payload); free(msg);

            pthread_mutex_lock(&s->send_queue_mutex);
            if (s->send_queue_head) lws_callback_on_writable(wsi);
            pthread_mutex_unlock(&s->send_queue_mutex);
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            dbg("LWS", "LWS_CALLBACK_RECEIVE");
            if (user) {
                ServerPerSessionData *psd = (ServerPerSessionData *)user;
                if (psd->session) {
                    char* buf = (char*)malloc(len + 1);
                    if(buf) {
                        memcpy(buf, in, len); buf[len] = '\0';
                        queue_push(&psd->session->incoming_queue, buf);
                        dbg("LWS", "Message pushed to session queue");
                    }
                }
            } else {
                AngaraLwsClient *c = (AngaraLwsClient *)context_user_data;
                char* buf = (char*)malloc(len + 1);
                if(buf) {
                    memcpy(buf, in, len); buf[len] = '\0';
                    queue_push(&c->incoming_queue, buf);
                }
            }
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            dbg("LWS", "LWS_CALLBACK_CLOSED");
            if (user) {
                ServerPerSessionData *psd = (ServerPerSessionData *)user;
                if (psd->session) {
                    psd->session->is_connected = false;
                    psd->session->wsi = NULL;
                    psd->session = NULL;
                }
            } else {
                AngaraLwsClient *c = (AngaraLwsClient *)context_user_data;
                c->is_connected = false;
                c->wsi = NULL;
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            dbg("LWS", "Client Connected");
            AngaraLwsClient *c = (AngaraLwsClient *)context_user_data;
            c->is_connected = true;
            lws_callback_on_writable(wsi);
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            AngaraLwsClient *c = (AngaraLwsClient *)context_user_data;
            pthread_mutex_lock(&c->send_queue_mutex);
            if (!c->send_queue_head) { pthread_mutex_unlock(&c->send_queue_mutex); break; }
            msg_buffer* msg = c->send_queue_head;
            c->send_queue_head = msg->next;
            pthread_mutex_unlock(&c->send_queue_mutex);

            lws_write(wsi, ((unsigned char*)msg->payload) + LWS_PRE, msg->len, LWS_WRITE_TEXT);
            free(msg->payload); free(msg);

            pthread_mutex_lock(&c->send_queue_mutex);
            if (c->send_queue_head) lws_callback_on_writable(wsi);
            pthread_mutex_unlock(&c->send_queue_mutex);
            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            dbg("LWS", "Client Connection Error");
            ((AngaraLwsClient *)context_user_data)->is_connected = false;
            break;
        }
        default: break;
    }
    return 0;
}

// --- ABI Functions ---

AngaraObject Angara_websocket_createServer(int arg_count, AngaraObject* args) {
    dbg("createServer", "Init");
    lws_set_log_level(0, NULL);
    if (arg_count < 2 || !IS_I64(args[0])) return angara_create_nil();
    int port = (int)AS_I64(args[0]);

    AngaraLwsServer* server = (AngaraLwsServer*)calloc(1, sizeof(AngaraLwsServer));
    queue_init(&server->accept_queue);

    server->protocols = (struct lws_protocols*)calloc(2, sizeof(struct lws_protocols));
    server->protocols[0].name = "http";
    server->protocols[0].callback = angara_lws_callback;
    server->protocols[0].per_session_data_size = sizeof(ServerPerSessionData);
    server->protocols[0].rx_buffer_size = 4096;
    server->protocols[1].name = NULL;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.user = server;
    info.protocols = server->protocols;

    server->context = lws_create_context(&info);
    if (!server->context) {
        free(server->protocols);
        free(server);
        return angara_create_nil();
    }

    AngaraObject self = angara_create_native_instance(server, finalize_server, "Server");
    server->self_obj = self;
    return self;
}

AngaraObject Angara_websocket_connect(int arg_count, AngaraObject* args) {
    dbg("connect", "Init");
    lws_set_log_level(0, NULL);
    if (arg_count != 1 || !IS_STRING(args[0])) return angara_create_nil();

    AngaraLwsClient* client = (AngaraLwsClient*)calloc(1, sizeof(AngaraLwsClient));
    client->header.type = NATIVE_TYPE_CLIENT;
    queue_init(&client->incoming_queue);
    pthread_mutex_init(&client->send_queue_mutex, NULL);
    generate_id(client->id, sizeof(client->id));

    client->protocols = (struct lws_protocols*)calloc(2, sizeof(struct lws_protocols));
    client->protocols[0].name = "http";
    client->protocols[0].callback = angara_lws_callback;
    client->protocols[0].per_session_data_size = 0;
    client->protocols[0].rx_buffer_size = 4096;
    client->protocols[1].name = NULL;

    struct lws_context_creation_info info = {NULL};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.user = client;
    info.protocols = client->protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    client->context = lws_create_context(&info);

    if (!client->context) {
        free(client->protocols);
        free(client);
        angara_throw_error("Failed to create context");
        return angara_create_nil();
    }

    client->self_obj = angara_create_native_instance(client, finalize_client, "WebSocket");

    char* url = strdup(AS_CSTRING(args[0]));
    const char *prot, *addr, *path; int port;
    if (lws_parse_uri(url, &prot, &addr, &port, &path)) {
        free(url); return angara_create_nil();
    }

    struct lws_client_connect_info cinfo = {0};
    cinfo.context = client->context;
    cinfo.address = addr;
    cinfo.port = port;
    cinfo.path = (path && *path) ? path : "/";
    cinfo.host = addr;
    cinfo.origin = addr;
    cinfo.protocol = "http";
    if (!strcmp(prot, "wss")) cinfo.ssl_connection = LCCSCF_USE_SSL;
    cinfo.pwsi = &client->wsi;
    cinfo.userdata = client;

    lws_client_connect_via_info(&cinfo);
    free(url);
    return client->self_obj;
}

AngaraObject Angara_WebSocket_send(int arg_count, AngaraObject* args) {
    if (!IS_STRING(args[1])) return angara_create_nil();
    void* native = AS_NATIVE_INSTANCE(args[0])->data;
    NativeObjectHeader* h = (NativeObjectHeader*)native;

    size_t len = AS_STRING(args[1])->length;
    void* payload = malloc(LWS_PRE + len);
    if (!payload) return angara_create_nil();
    memcpy((char*)payload + LWS_PRE, AS_CSTRING(args[1]), len);

    msg_buffer* node = (msg_buffer*)malloc(sizeof(msg_buffer));
    node->payload = payload; node->len = len; node->next = NULL;

    struct lws* wsi = NULL;
    pthread_mutex_t* mutex = NULL;
    msg_buffer** head_ptr = NULL;

    if (h->type == NATIVE_TYPE_CLIENT) {
        AngaraLwsClient* c = (AngaraLwsClient*)native;
        if (!c->is_connected) { free(payload); free(node); return angara_create_nil(); }
        wsi = c->wsi;
        mutex = &c->send_queue_mutex;
        head_ptr = &c->send_queue_head;
    } else {
        AngaraLwsSession* s = (AngaraLwsSession*)native;
        if (!s->is_connected) { free(payload); free(node); return angara_create_nil(); }
        wsi = s->wsi;
        mutex = &s->send_queue_mutex;
        head_ptr = &s->send_queue_head;
    }

    pthread_mutex_lock(mutex);
    if (!*head_ptr) *head_ptr = node;
    else {
        msg_buffer* t = *head_ptr;
        while(t->next) t = t->next;
        t->next = node;
    }
    pthread_mutex_unlock(mutex);

    if (h->type == NATIVE_TYPE_CLIENT) lws_cancel_service(((AngaraLwsClient*)native)->context);
    else lws_callback_on_writable(wsi);

    return angara_create_nil();
}

AngaraObject Angara_WebSocket_read(int arg_count, AngaraObject* args) {
    // dbg("read", "Called");
    void* native = AS_NATIVE_INSTANCE(args[0])->data;
    NativeObjectHeader* h = (NativeObjectHeader*)native;
    ConcurrentQueue* q;

    if (h->type == NATIVE_TYPE_CLIENT) {
        q = &((AngaraLwsClient*)native)->incoming_queue;
    } else if (h->type == NATIVE_TYPE_SERVER_CONNECTION) {
        q = &((AngaraLwsSession*)native)->incoming_queue;
    } else {
        fprintf(stderr, "[WS ERROR] read() called on unknown native type %d\n", h->type);
        return angara_create_nil();
    }

    char* msg = (char*)queue_pop(q);
    if (msg) {
        // dbg("read", "Got message");
        AngaraObject s = angara_create_string(msg);
        free(msg);
        return s;
    }
    return angara_create_nil();
}

AngaraObject Angara_WebSocket_is_open(int arg_count, AngaraObject* args) {
    void* native = AS_NATIVE_INSTANCE(args[0])->data;
    NativeObjectHeader* h = (NativeObjectHeader*)native;

    if (h->type == NATIVE_TYPE_CLIENT) {
        return angara_create_bool(((AngaraLwsClient*)native)->is_connected);
    }

    if (h->type == NATIVE_TYPE_SERVER_CONNECTION) {
        return angara_create_bool(((AngaraLwsSession*)native)->is_connected);
    }

    fprintf(stderr, "[WS ERROR] is_open called on invalid native pointer %p type %d\n", native, h->type);
    return angara_create_bool(false);
}

AngaraObject Angara_WebSocket_get_id(int arg_count, AngaraObject* args) {
    void* native = AS_NATIVE_INSTANCE(args[0])->data;
    NativeObjectHeader* h = (NativeObjectHeader*)native;
    if (h->type == NATIVE_TYPE_CLIENT) return angara_create_string(((AngaraLwsClient*)native)->id);
    return angara_create_string(((AngaraLwsSession*)native)->id);
}

AngaraObject Angara_WebSocket_close(int arg_count, AngaraObject* args) {
    return angara_create_nil();
}

AngaraObject Angara_WebSocket_service(int arg_count, AngaraObject* args) {
    void* native = AS_NATIVE_INSTANCE(args[0])->data;
    if (((NativeObjectHeader*)native)->type == NATIVE_TYPE_CLIENT) {
        AngaraLwsClient* c = (AngaraLwsClient*)native;
        if(c->context) lws_service(c->context, 0);
    }
    return angara_create_nil();
}

AngaraObject Angara_Server_service(int arg_count, AngaraObject* args) {
    // dbg("server.service", "Tick");
    AngaraLwsServer* s = (AngaraLwsServer*)AS_NATIVE_INSTANCE(args[0])->data;
    if (s->context) lws_service(s->context, 0);
    return angara_create_nil();
}

AngaraObject Angara_Server_accept(int arg_count, AngaraObject* args) {
    AngaraLwsServer* s = (AngaraLwsServer*)AS_NATIVE_INSTANCE(args[0])->data;
    AngaraObject* ptr = (AngaraObject*)queue_pop(&s->accept_queue);
    if (ptr) {
        dbg("accept", "Returning new connection");
        AngaraObject o = *ptr;
        free(ptr);
        return o;
    }
    return angara_create_nil();
}

// ... Exports ... (Include your exports block here)
static const AngaraMethodDef WEBSOCKET_METHODS[] = {
        {"send",    (AngaraMethodFn)Angara_WebSocket_send,     "s->n"},
        {"read",    (AngaraMethodFn)Angara_WebSocket_read,     "->s?"},
        {"close",   (AngaraMethodFn)Angara_WebSocket_close,    "->n"},
        {"service", (AngaraMethodFn)Angara_WebSocket_service,  "->n"},
        {"is_open", (AngaraMethodFn)Angara_WebSocket_is_open,  "->b"},
        {"get_id",  (AngaraMethodFn)Angara_WebSocket_get_id,   "->s"},
        {NULL, NULL, NULL}
};
static const AngaraMethodDef SERVER_METHODS[] = {
        {"service", (AngaraMethodFn)Angara_Server_service, "->n"},
        {"accept",  (AngaraMethodFn)Angara_Server_accept,  "->WebSocket?"},
        {NULL, NULL, NULL}
};
static const AngaraClassDef WEBSOCKET_CLASS_DEF = { "WebSocket", NULL, WEBSOCKET_METHODS };
static const AngaraClassDef SERVER_CLASS_DEF = { "Server", NULL, SERVER_METHODS };
static const AngaraFuncDef WEBSOCKET_EXPORTS[] = {
        {"connect",      Angara_websocket_connect,      "s{}->WebSocket", &WEBSOCKET_CLASS_DEF},
        {"createServer", Angara_websocket_createServer, "i{}->Server",   &SERVER_CLASS_DEF},
        {NULL, NULL, NULL, NULL}
};
ANGARA_MODULE_INIT(websocket) {
        *def_count = (sizeof(WEBSOCKET_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
        return WEBSOCKET_EXPORTS;
}
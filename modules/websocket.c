#include "../src/runtime/angara_runtime.h"
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>


static int angara_lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len);
void finalize_server(void* data);
void finalize_client(void* data);

typedef enum {
    NATIVE_TYPE_CLIENT,
    NATIVE_TYPE_SERVER_CONNECTION
} NativeObjectType;

typedef struct {
    NativeObjectType type;
} NativeObjectHeader;

// --- Data Structures ---

typedef struct msg_buffer { void *payload; size_t len; struct msg_buffer *next; } msg_buffer;
typedef struct AngaraLwsServer {
    AngaraObject self_obj;
    struct lws_context *context;
    AngaraObject on_connect_closure;
    AngaraObject on_message_closure;
    AngaraObject on_close_closure;
} AngaraLwsServer;
typedef struct AngaraLwsClient {
    NativeObjectHeader header;
    struct lws_context *context;
    struct lws *wsi;
    bool is_connected;
    msg_buffer *msg_queue_head;
    AngaraObject self_obj;
    AngaraObject on_open_closure;
    AngaraObject on_message_closure;
    AngaraObject on_close_closure;
    AngaraObject on_error_closure;
    pthread_mutex_t send_queue_mutex;
} AngaraLwsClient;
typedef struct ServerPerSessionData {
    NativeObjectHeader header;
    struct lws *wsi;
    AngaraObject client_obj;
    msg_buffer *msg_queue_head;
} ServerPerSessionData;


// --- Main LWS Callback ---

static int angara_lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len) {
    void* context_user_data = lws_context_user(lws_get_context(wsi));
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            ServerPerSessionData *psd = (ServerPerSessionData *)user;
            AngaraLwsServer *server = (AngaraLwsServer *)context_user_data;
            psd->header.type = NATIVE_TYPE_SERVER_CONNECTION;
            psd->wsi = wsi;
            psd->client_obj = angara_create_native_instance(psd, nullptr);
            angara_incref(psd->client_obj);
            if (!IS_NIL(server->on_connect_closure)) {
                angara_call(server->on_connect_closure, 2, (AngaraObject[]){server->self_obj, psd->client_obj});
            }
            break;
        }
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            ServerPerSessionData *psd = (ServerPerSessionData *)user;
            if (!psd->msg_queue_head) break;
            msg_buffer* current_msg = psd->msg_queue_head;
            int bytes_sent = lws_write(wsi, ((unsigned char*)current_msg->payload) + LWS_PRE, current_msg->len, LWS_WRITE_TEXT);
            if (bytes_sent < (int)current_msg->len) { return -1; }
            psd->msg_queue_head = current_msg->next;
            free(current_msg->payload); free(current_msg);
            if (psd->msg_queue_head) lws_callback_on_writable(wsi);
            break;
        }
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            AngaraLwsClient *client = (AngaraLwsClient *)context_user_data;
            client->is_connected = true;
            if (!IS_NIL(client->on_open_closure)) {
                angara_call(client->on_open_closure, 1, (AngaraObject[]){client->self_obj});
            }
            lws_callback_on_writable(wsi);
            break;
        }
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            AngaraLwsClient *client = (AngaraLwsClient *)context_user_data;
            msg_buffer* msg_to_send = nullptr;

            // Lock the mutex before touching the queue ---
            pthread_mutex_lock(&client->send_queue_mutex);
            if (client->msg_queue_head) {
                // Dequeue the message
                msg_to_send = client->msg_queue_head;
                client->msg_queue_head = msg_to_send->next;
            }
            pthread_mutex_unlock(&client->send_queue_mutex);
            // --- End of critical section ---

            if (!msg_to_send) break;

            int bytes_sent = lws_write(wsi, ((unsigned char*)msg_to_send->payload) + LWS_PRE, msg_to_send->len, LWS_WRITE_TEXT);
            if (bytes_sent < (int)msg_to_send->len) { /* handle error */ }

            // Free the memory *after* sending
            free(msg_to_send->payload);
            free(msg_to_send);

            // Check if there are more messages in the queue (thread-safe)
            pthread_mutex_lock(&client->send_queue_mutex);
            if (client->msg_queue_head) {
                lws_callback_on_writable(wsi); // Ask to be called again
            }
            pthread_mutex_unlock(&client->send_queue_mutex);

            break;
        }
        case LWS_CALLBACK_RECEIVE: {
            if (user) {
                ServerPerSessionData *psd = (ServerPerSessionData *)user;
                AngaraLwsServer *server = (AngaraLwsServer *)context_user_data;
                if (!IS_NIL(server->on_message_closure)) {
                    char* buffer = (char*)malloc(len + 1);
                    if (!buffer) { return -1; }
                    memcpy(buffer, in, len);
                    buffer[len] = '\0';
                    AngaraObject msg = angara_create_string(buffer);
                    free(buffer);
                    angara_call(server->on_message_closure, 3, (AngaraObject[]){server->self_obj, psd->client_obj, msg});
                    angara_decref(msg);
                }
            } else {
                AngaraLwsClient *client = (AngaraLwsClient *)context_user_data;
                if (!IS_NIL(client->on_message_closure)) {
                    char* buffer = (char*)malloc(len + 1);
                    if (!buffer) { return -1; }
                    memcpy(buffer, in, len);
                    buffer[len] = '\0';
                    AngaraObject msg = angara_create_string(buffer);
                    free(buffer);
                    angara_call(client->on_message_closure, 2, (AngaraObject[]){client->self_obj, msg});
                    angara_decref(msg);
                }
            }
            break;
        }
        case LWS_CALLBACK_CLOSED: {
            if (user) {
                ServerPerSessionData *psd = (ServerPerSessionData *)user;
                AngaraLwsServer *server = (AngaraLwsServer *)context_user_data;
                if (!IS_NIL(server->on_close_closure)) {
                    angara_call(server->on_close_closure, 2, (AngaraObject[]){server->self_obj, psd->client_obj});
                }
                angara_decref(psd->client_obj);
            } else {
                AngaraLwsClient *client = (AngaraLwsClient *)context_user_data;
                client->is_connected = false;
                if (!IS_NIL(client->on_close_closure)) {
                    angara_call(client->on_close_closure, 1, (AngaraObject[]){client->self_obj});
                }
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            AngaraLwsClient *client = (AngaraLwsClient *)context_user_data;
            client->is_connected = false;
            if (!IS_NIL(client->on_error_closure)) {
                const char* err_msg_str = in ? (const char*)in : "Unknown connection error";
                AngaraObject err_msg = angara_create_string(err_msg_str);
                angara_call(client->on_error_closure, 2, (AngaraObject[]){ client->self_obj, err_msg });
                angara_decref(err_msg);
            }
            break;
        }
        default:
            break;
    }
    return 0;
}


// --- Angara ABI Functions ---

AngaraObject Angara_websocket_createServer(int arg_count, AngaraObject* args) {
    lws_set_log_level(0, nullptr);
    if (arg_count < 2 || !IS_I64(args[0]) || !IS_RECORD(args[1])) {
        angara_throw_error("createServer(port, callbacks, [options]) expects an integer, a record, and an optional options record.");
        return angara_create_nil();
    }
    int port = (int)AS_I64(args[0]);
    AngaraObject callbacks = args[1];
    AngaraLwsServer* server_data = (AngaraLwsServer*)calloc(1, sizeof(AngaraLwsServer));
    server_data->on_connect_closure = angara_record_get(callbacks, "on_connect");
    server_data->on_message_closure = angara_record_get(callbacks, "on_message");
    server_data->on_close_closure = angara_record_get(callbacks, "on_close");
    angara_incref(server_data->on_connect_closure);
    angara_incref(server_data->on_message_closure);
    angara_incref(server_data->on_close_closure);
    AngaraObject self = angara_create_native_instance(server_data, finalize_server);
    server_data->self_obj = self;
    angara_incref(self);
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port;
    info.user = server_data;
    info.protocols = (struct lws_protocols[]){
            {"http", angara_lws_callback, sizeof(ServerPerSessionData), 4096},
            {nullptr, nullptr, 0, 0}
    };
    if (arg_count == 3 && IS_RECORD(args[2])) {
        AngaraObject options = args[2];
        AngaraObject cert_path = angara_record_get(options, "cert");
        AngaraObject key_path = angara_record_get(options, "key");
        if (IS_STRING(cert_path) && IS_STRING(key_path)) {
            info.ssl_cert_filepath = AS_CSTRING(cert_path);
            info.ssl_private_key_filepath = AS_CSTRING(key_path);
            info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        }
    }
    server_data->context = lws_create_context(&info);
    if (!server_data->context) {
        angara_throw_error("Failed to create libwebsockets server context.");
        finalize_server(server_data);
        return angara_create_nil();
    }
    return self;
}

AngaraObject Angara_websocket_connect(int arg_count, AngaraObject* args) {
    lws_set_log_level(0, nullptr);
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_RECORD(args[1])) {
        angara_throw_error("connect(url, callbacks) expects a string and a record.");
        return angara_create_nil();
    }
    const auto client_data = (AngaraLwsClient*)calloc(1, sizeof(AngaraLwsClient));
    client_data->header.type = NATIVE_TYPE_CLIENT;

    if (pthread_mutex_init(&client_data->send_queue_mutex, nullptr) != 0) {
        angara_throw_error("Failed to initialize client mutex.");
        free(client_data);
        return angara_create_nil();
    }

    client_data->self_obj = angara_create_native_instance(client_data, finalize_client);
    angara_incref(client_data->self_obj);
    client_data->on_open_closure = angara_record_get(args[1], "on_open");
    client_data->on_message_closure = angara_record_get(args[1], "on_message");
    client_data->on_close_closure = angara_record_get(args[1], "on_close");
    client_data->on_error_closure = angara_record_get(args[1], "on_error");
    angara_incref(client_data->on_open_closure); angara_incref(client_data->on_message_closure);
    angara_incref(client_data->on_close_closure); angara_incref(client_data->on_error_closure);
    struct lws_context_creation_info info = {nullptr};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.user = client_data;
    info.protocols = (struct lws_protocols[]){ {"http", angara_lws_callback, 0, 4096}, {nullptr, nullptr, 0, 0} };
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    client_data->context = lws_create_context(&info);
    if (!client_data->context) {
        angara_throw_error("Failed to create libwebsockets client context.");
        finalize_client(client_data);
        return angara_create_nil();
    }
    char* url_copy = strdup(AS_CSTRING(args[0]));
    const char *protocol, *host, *parsed_path;
    int port;

    if (lws_parse_uri(url_copy, &protocol, &host, &port, &parsed_path)) {
        angara_throw_error("Invalid WebSocket URL");
        free(url_copy);
        finalize_client(client_data);
        return angara_create_nil();
    }

    // Ensure the path is valid. lws_parse_uri can return an empty string ""
    // for a URL like "ws://example.com", which is not a valid HTTP request path.
    char path[256] = "/";
    if (parsed_path && *parsed_path) {
        strncpy(path, parsed_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    struct lws_client_connect_info conn_info;
    memset(&conn_info, 0, sizeof(conn_info));
    conn_info.context = client_data->context;
    conn_info.address = host;
    conn_info.port = port;
    conn_info.path = path; // Use our guaranteed-valid path
    conn_info.host = conn_info.address;
    conn_info.origin = conn_info.address;
    conn_info.protocol = "http";
    if (strcmp(protocol, "wss") == 0) {
        conn_info.ssl_connection = LCCSCF_USE_SSL;
    }
    conn_info.pwsi = &client_data->wsi;

    // Start the connection attempt
    if (!lws_client_connect_via_info(&conn_info)) {
        // This is a fatal error, the connection could not even be started.
        // We need to clean up and inform the user.
        angara_throw_error("Failed to start WebSocket client connection.");
        free(url_copy);
        finalize_client(client_data); // This will decref self_obj
        return angara_create_nil();
    }

    free(url_copy);


    return client_data->self_obj;
}

AngaraObject Angara_WebSocket_send(__attribute__((unused)) int arg_count, AngaraObject* args) {
    void* native_data = AS_NATIVE_INSTANCE(args[0])->data; AngaraObject message = args[1];
    size_t msg_len = AS_STRING(message)->length;
    void* msg_payload = malloc(LWS_PRE + msg_len);
    memcpy((char*)msg_payload + LWS_PRE, AS_CSTRING(message), msg_len);
    msg_buffer* new_msg = (msg_buffer*)malloc(sizeof(msg_buffer));
    new_msg->payload = msg_payload; new_msg->len = msg_len; new_msg->next = nullptr;
    NativeObjectHeader* header = (NativeObjectHeader*)native_data;
    if (header->type == NATIVE_TYPE_CLIENT) {
        AngaraLwsClient* client = (AngaraLwsClient*)native_data;

        // --- Critical section ---
        pthread_mutex_lock(&client->send_queue_mutex);

        // Correctly append to the END of the linked list
        if (!client->msg_queue_head) {
            client->msg_queue_head = new_msg;
        } else {
            msg_buffer* tail = client->msg_queue_head;
            while (tail->next) {
                tail = tail->next;
            }
            tail->next = new_msg;
        }

        pthread_mutex_unlock(&client->send_queue_mutex);
        // --- End of critical section ---

        lws_callback_on_writable(client->wsi);
    }else {
        ServerPerSessionData* psd = (ServerPerSessionData*)native_data;
        if (psd->msg_queue_head) psd->msg_queue_head->next = new_msg; else psd->msg_queue_head = new_msg;
        lws_callback_on_writable(psd->wsi);
    }
    return angara_create_nil();
}

AngaraObject Angara_WebSocket_close(__attribute__((unused)) int arg_count, AngaraObject* args) {
    void* native_data = AS_NATIVE_INSTANCE(args[0])->data; NativeObjectHeader* header = (NativeObjectHeader*)native_data;
    if (header->type == NATIVE_TYPE_CLIENT) {
        AngaraLwsClient* client = (AngaraLwsClient*)native_data;
        lws_close_reason(client->wsi, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
        client->is_connected = false;
    } else {
        ServerPerSessionData* psd = (ServerPerSessionData*)native_data;
        lws_close_reason(psd->wsi, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
    }
    return angara_create_nil();
}

AngaraObject Angara_WebSocket_is_open(__attribute__((unused)) int arg_count, AngaraObject* args) {
    void* native_data = AS_NATIVE_INSTANCE(args[0])->data;
    const auto header = (NativeObjectHeader*)native_data;
    if (header->type == NATIVE_TYPE_CLIENT) {
        const auto client = (AngaraLwsClient*)native_data;
        return angara_create_bool(client->is_connected);
    }
    return angara_create_bool(true);
}

AngaraObject Angara_WebSocket_service(__attribute__((unused)) int arg_count, AngaraObject* args) {
    AngaraLwsClient* client = (AngaraLwsClient*)AS_NATIVE_INSTANCE(args[0])->data;
    if (client->context) { lws_service(client->context, 0); }
    return angara_create_nil();
}

AngaraObject Angara_Server_service(__attribute__((unused)) int arg_count, AngaraObject* args) {
    AngaraLwsServer* server = (AngaraLwsServer*)AS_NATIVE_INSTANCE(args[0])->data;
    if (server->context) { lws_service(server->context, 0); }
    return angara_create_nil();
}

void finalize_server(void* data) {
    AngaraLwsServer* server = (AngaraLwsServer*)data;
    if (server->context) lws_context_destroy(server->context);
    angara_decref(server->self_obj);
    angara_decref(server->on_connect_closure);
    angara_decref(server->on_message_closure);
    angara_decref(server->on_close_closure);
    free(server);
}

void finalize_client(void* data) {
    AngaraLwsClient* client = (AngaraLwsClient*)data;
    if (client->context) lws_context_destroy(client->context);
    msg_buffer *current = client->msg_queue_head;
    while(current) {
        msg_buffer* next = current->next;
        free(current->payload); free(current);
        current = next;
    }
    angara_decref(client->self_obj);
    angara_decref(client->on_open_closure);
    angara_decref(client->on_message_closure);
    angara_decref(client->on_close_closure);
    angara_decref(client->on_error_closure);
    free(client);
}


// --- ABI Definitions ---

static const AngaraMethodDef WEBSOCKET_METHODS[] = {
        {"send",    (AngaraMethodFn)Angara_WebSocket_send,     "s->n"},
        {"close",   (AngaraMethodFn)Angara_WebSocket_close,    "->n"},
        {"service", (AngaraMethodFn)Angara_WebSocket_service,  "->n"},
        {"is_open", (AngaraMethodFn)Angara_WebSocket_is_open,  "->b"},
        {nullptr, nullptr, nullptr}
};

static const AngaraMethodDef SERVER_METHODS[] = {
        {"service", (AngaraMethodFn)Angara_Server_service, "->n"},
        {nullptr, nullptr, nullptr}
};

static const AngaraClassDef WEBSOCKET_CLASS_DEF = { "WebSocket", nullptr, WEBSOCKET_METHODS };
static const AngaraClassDef SERVER_CLASS_DEF = { "Server", nullptr, SERVER_METHODS };

static const AngaraFuncDef WEBSOCKET_EXPORTS[] = {
        {"connect",      Angara_websocket_connect,      "s{}->WebSocket", &WEBSOCKET_CLASS_DEF},
        {"createServer", Angara_websocket_createServer, "i{}->Server",   &SERVER_CLASS_DEF},
        {nullptr, nullptr, nullptr, nullptr}
};

ANGARA_MODULE_INIT(websocket) {
        *def_count = (sizeof(WEBSOCKET_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
        return WEBSOCKET_EXPORTS;
}
//
// Created by cv2 on 9/14/25.
//
#include "../src/runtime/angara_runtime.h" // The one, true header
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>

// --- Private Data Structure ---
// This struct holds all the state for a single WebSocket connection.
// It is stored in the `data` pointer of the AngaraNativeInstance.
typedef struct {
    struct lws_context *context;
    struct lws *wsi; // The websocket instance
    bool is_connected;
    bool should_close;

    // We store the Angara closures for the event callbacks.
    AngaraObject on_open_closure;
    AngaraObject on_message_closure;
    AngaraObject on_close_closure;
    AngaraObject on_error_closure;
} WebSocketData;


// --- libwebsockets Event Callback ---
// This is the single C function that libwebsockets calls for all events.
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len)
{
    // `lws_get_opaque_user_data` retrieves the pointer we associated with this connection.
    // We cleverly associate it with the AngaraNativeInstance itself.
    AngaraNativeInstance* instance = (AngaraNativeInstance*)lws_get_opaque_user_data(wsi);
    if (!instance) { return 0; }

    // Re-create the AngaraObject handle to pass to callbacks.
    AngaraObject self = { VAL_OBJ, { .obj = (Object*)instance } };
    WebSocketData* data = (WebSocketData*)instance->data;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            data->is_connected = true;
            if (!IS_NIL(data->on_open_closure)) {
                angara_call(data->on_open_closure, 1, (AngaraObject[]){ self });
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            // The received data is in `in`. Create an AngaraString.
            // libwebsockets guarantees that `in` is null-terminated if it's text.
            if (!IS_NIL(data->on_message_closure)) {
                AngaraObject msg = angara_create_string((const char*)in);
                angara_call(data->on_message_closure, 2, (AngaraObject[]){ self, msg });
                angara_decref(msg);
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            data->is_connected = false;
            if (!IS_NIL(data->on_error_closure)) {
                AngaraObject err_msg = angara_create_string(in ? (char *)in : "Unknown connection error");
                angara_call(data->on_error_closure, 2, (AngaraObject[]){ self, err_msg });
                angara_decref(err_msg);
            }
            // Fall through to also trigger on_close
        case LWS_CALLBACK_CLIENT_CLOSED:
            if (data->is_connected) { // Prevent double-calling on_close
                data->is_connected = false;
                if (!IS_NIL(data->on_close_closure)) {
                    angara_call(data->on_close_closure, 1, (AngaraObject[]){ self });
                }
            }
            // Signal the service loop to exit
            data->should_close = true;
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            // This is where we would handle sending queued messages.
            // For a simple implementation, we handle sends immediately.
            break;

        default:
            break;
    }
    return 0;
}

static struct lws_protocols protocols[] = { { "lws-angara", websocket_callback, 0, 2048 }, { NULL, NULL, 0, 0 } };

// --- Finalizer ---
void finalize_websocket(void* data_ptr) {
    WebSocketData* data = (WebSocketData*)data_ptr;
    angara_debug_print("Finalizing WebSocket object.");
    if (data->context) {
        lws_context_destroy(data->context);
    }
    angara_decref(data->on_open_closure);
    angara_decref(data->on_message_closure);
    angara_decref(data->on_close_closure);
    angara_decref(data->on_error_closure);
    free(data);
}

// --- Constructor ---
// Angara: websocket.connect(url as string, callbacks as record) -> WebSocket
AngaraObject Angara_websocket_connect(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_RECORD(args[1])) {
        angara_throw_error("connect(url, callbacks) expects a string and a record.");
        return angara_create_nil();
    }
    const char* url = AS_CSTRING(args[0]);
    AngaraObject callbacks = args[1];

    // 1. Allocate our private data struct
    WebSocketData* data = (WebSocketData*)calloc(1, sizeof(WebSocketData));
    data->on_open_closure = angara_record_get(callbacks, "on_open");
    data->on_message_closure = angara_record_get(callbacks, "on_message");
    data->on_close_closure = angara_record_get(callbacks, "on_close");
    data->on_error_closure = angara_record_get(callbacks, "on_error");
    // We now own these closures, so incref them.
    angara_incref(data->on_open_closure);
    angara_incref(data->on_message_closure);
    angara_incref(data->on_close_closure);
    angara_incref(data->on_error_closure);

    // 2. Create the Angara object container
    AngaraObject self = angara_create_native_instance(data, finalize_websocket);

    // 3. Set up libwebsockets context
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.client_ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt"; // For Linux; may need adjustment

    data->context = lws_create_context(&info);
    if (!data->context) { /* ... error handling ... */ }

    // 4. Set up connection info, parsing the URL
    struct lws_client_connect_info i;
    memset(&i, 0, sizeof(i));
    char host[256], path[256];

    if (lws_parse_uri(url, &i.protocol, &i.address, &i.port, &i.path)) {
        angara_throw_error("Invalid WebSocket URL.");
        // decref self to trigger finalizer and cleanup
        angara_decref(self);
        return angara_create_nil();
    }
    i.context = data->context;
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED;
    i.protocol = protocols[0].name;
    i.pwsi = &data->wsi;
    // THIS IS THE KEY: Associate the Angara object with the connection
    i.opaque_user_data = AS_NATIVE_INSTANCE(self);

    lws_client_connect_via_info(&i);

    return self;
}

// --- Methods ---
AngaraObject Angara_WebSocket_send(AngaraObject self, int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) return angara_create_nil();
    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;
    if (!data->is_connected) return angara_create_nil();

    AngaraString* msg = AS_STRING(args[0]);
    // lws_write requires LWS_PRE bytes of padding before the message
    unsigned char* buf = (unsigned char*)malloc(LWS_PRE + msg->length);
    memcpy(buf + LWS_PRE, msg->chars, msg->length);
    lws_write(data->wsi, buf + LWS_PRE, msg->length, LWS_WRITE_TEXT);
    free(buf);

    return angara_create_nil();
}

AngaraObject Angara_WebSocket_close(AngaraObject self, int arg_count, AngaraObject* args) {
    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;
    if (!data->is_connected) return angara_create_nil();
    data->should_close = true;
    lws_close_reason(data->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
    return angara_create_nil();
}

AngaraObject Angara_WebSocket_service(AngaraObject self, int arg_count, AngaraObject* args) {
    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;
    if (data->context) {
        lws_service(data->context, 0);
    }
    return angara_create_nil();
}

AngaraObject Angara_WebSocket_is_open(AngaraObject self, int arg_count, AngaraObject* args) {
    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;
    return angara_create_bool(data->is_connected && !data->should_close);
}

// --- ABI Definitions ---
static const AngaraMethodDef WEBSOCKET_METHODS[] = {
        {"send",     Angara_WebSocket_send,     "s->n"},
        {"close",    Angara_WebSocket_close,    "->n"},
        {"service",  Angara_WebSocket_service,  "->n"},
        {"is_open",  Angara_WebSocket_is_open,  "->b"},
        {NULL, NULL, NULL}
};

static const AngaraClassDef WEBSOCKET_CLASS = {
        .class_name       = "WebSocket",
        .constructor_name = "connect",
        .constructor_func = (AngaraCtorFn)Angara_websocket_connect,
        .ctor_type_string = "s{}->a", // string, record -> any (WebSocket instance)
        .fields           = NULL,
        .methods          = WEBSOCKET_METHODS
};

ANGARA_MODULE_INIT(websocket) {
        static const AngaraModuleDef DEFS[] = {
            ANGARA_CLASS(&WEBSOCKET_CLASS),
                    ANGARA_SENTINEL
        };
        *def_count = 1;
        return DEFS;
}
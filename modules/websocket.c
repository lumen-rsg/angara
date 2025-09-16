#include "../src/runtime/angara_runtime.h"
#include <wslay/wslay.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h> // For non-blocking sockets
#include <openssl/rand.h> // For random key generation
#include <openssl/sha.h>  // For SHA-1 hashing
#include <openssl/evp.h>  // For Base64 encoding
#include <ctype.h>


// --- Private Data Structure ---
// Holds all state for a single WebSocket connection.
typedef struct {
    int fd;
    SSL *ssl;
    SSL_CTX *ssl_ctx;
    wslay_event_context_ptr ctx;
    bool is_connected;
    bool should_close;
    AngaraObject self_obj;
    AngaraObject on_open_closure;
    AngaraObject on_message_closure;
    AngaraObject on_close_closure;
    AngaraObject on_error_closure;
} WebSocketData;

// wslay calls this function to get 4 random bytes for the frame mask.
// We use OpenSSL's cryptographically secure random number generator.
int wslay_genmask_callback(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, void *user_data) {
    if (RAND_bytes(buf, len) != 1) {
        // Failed to get random bytes, signal a failure.
        return -1;
    }
    return 0; // Success
}


// --- wslay Event Callbacks ---
// wslay calls these functions to perform I/O on the underlying socket.

static char* base64_encode(const unsigned char* input, int length) {
    // Base64 output is roughly 4/3 the size of the input, plus padding.
    size_t out_len = 4 * ((length + 2) / 3);
    char *output = (char*)malloc(out_len + 1);
    int final_len = EVP_EncodeBlock((unsigned char*)output, input, length);
    output[final_len] = '\0';
    return output;
}

ssize_t wslay_send_callback(wslay_event_context_ptr ctx, const uint8_t *data, size_t len, int flags, void *user_data) {
    WebSocketData *ws = (WebSocketData*)user_data;
    ssize_t sent;
    ERR_clear_error();
    sent = SSL_write(ws->ssl, data, len);
    if (sent < 0) {
        int err = SSL_get_error(ws->ssl, sent);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    }
    return sent;
}

ssize_t wslay_recv_callback(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, int flags, void *user_data) {
    WebSocketData *ws = (WebSocketData*)user_data;
    ssize_t received;
    ERR_clear_error();
    received = SSL_read(ws->ssl, buf, len);
    if (received < 0) {
        int err = SSL_get_error(ws->ssl, received);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    } else if (received == 0) { // Connection closed by peer
        wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    }
    return received;
}

// wslay calls this when a complete message has been received and parsed.
void wslay_on_msg_recv_callback(wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg *arg, void *user_data) {
    WebSocketData* data = (WebSocketData*)user_data;
    // We only care about text messages for this simple client.
    if (arg->opcode != WSLAY_TEXT_FRAME) return;

    if (!IS_NIL(data->on_message_closure)) {
        // Create a copy of the message, as the wslay buffer is temporary.
        AngaraObject msg = angara_create_string((const char*)arg->msg);
        angara_call(data->on_message_closure, 2, (AngaraObject[]){ data->self_obj, msg });
        angara_decref(msg);
    }
}


// --- Finalizer ---
// The runtime calls this when the Angara WebSocket object is garbage collected.
void finalize_websocket(void* data_ptr) {
    WebSocketData* data = (WebSocketData*)data_ptr;
    if (data->ctx) wslay_event_context_free(data->ctx);
    if (data->ssl) {
        SSL_shutdown(data->ssl);
        SSL_free(data->ssl);
    }
    if (data->ssl_ctx) SSL_CTX_free(data->ssl_ctx);
    if (data->fd >= 0) close(data->fd);

    angara_decref(data->on_open_closure);
    angara_decref(data->on_message_closure);
    angara_decref(data->on_close_closure);
    angara_decref(data->on_error_closure);
    // self_obj is not decref'd here to prevent double-free cycles.
    free(data);
}

// --- Constructor ---
// Angara: websocket.connect(url, callbacks)

AngaraObject Angara_websocket_connect(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_RECORD(args[1])) {
        angara_throw_error("connect(url, callbacks) expects a string and a record.");
        return angara_create_nil();
    }
    const char* url_str = AS_CSTRING(args[0]);
    AngaraObject callbacks = args[1];

    WebSocketData* data = (WebSocketData*)calloc(1, sizeof(WebSocketData));
    data->fd = -1;
    AngaraObject self = angara_create_native_instance(data, finalize_websocket);
    data->self_obj = self;
    angara_incref(self);

    data->on_open_closure = angara_record_get(callbacks, "on_open");
    // --- THIS IS THE MISSING LOGIC ---
    // 4. Extract the closures from the callbacks record.
    data->on_open_closure = angara_record_get(callbacks, "on_open");
    data->on_message_closure = angara_record_get(callbacks, "on_message");
    data->on_close_closure = angara_record_get(callbacks, "on_close");
    data->on_error_closure = angara_record_get(callbacks, "on_error");

    // 5. We now own these closures. We must increment their reference counts
    //    so they are not garbage collected while the WebSocket is alive.
    angara_incref(data->on_open_closure);
    angara_incref(data->on_message_closure);
    angara_incref(data->on_close_closure);
    angara_incref(data->on_error_closure);
    // --- END OF MISSING LOGIC ---

    // --- 1. Proper URL Parsing ---
    char scheme[16], host[256], path[256];
    int port;
    if (sscanf(url_str, "%15[^:]://%255[^:/]:%d%255s", scheme, host, &port, path) == 4) {
    } else if (sscanf(url_str, "%15[^:]://%255[^/]%255s", scheme, host, path) == 3) {
        port = (strcmp(scheme, "wss") == 0) ? 443 : 80;
    } else if (sscanf(url_str, "%15[^:]://%255[^:/]:%d", scheme, host, &port) == 3) {
        strcpy(path, "/");
    } else if (sscanf(url_str, "%15[^:]://%255[^/]", scheme, host) == 2) {
        port = (strcmp(scheme, "wss") == 0) ? 443 : 80;
        strcpy(path, "/");
    } else {
        angara_throw_error("Could not parse WebSocket URL.");
        angara_decref(self); return angara_create_nil();
    }

    bool use_ssl = (strcmp(scheme, "wss") == 0);
    char port_str[6];
    sprintf(port_str, "%d", port);

    // --- 2. Socket Connection (Now Synchronous/Blocking) ---
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        angara_throw_error("DNS lookup failed.");
        angara_decref(self); return angara_create_nil();
    }

    data->fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    // The socket is in BLOCKING mode by default. We connect synchronously.
    if (connect(data->fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        angara_throw_error("Socket connect failed.");
        angara_decref(self); return angara_create_nil();
    }
    freeaddrinfo(res);

    // --- 3. SSL/TLS Handshake (Now Synchronous/Blocking) ---
    if (use_ssl) {
        SSL_library_init();
        data->ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!data->ssl_ctx) { /* ... error ... */ }
        if (SSL_CTX_set_default_verify_paths(data->ssl_ctx) != 1) { /* ... error ... */ }

        data->ssl = SSL_new(data->ssl_ctx);
        SSL_set_fd(data->ssl, data->fd);
        SSL_set_tlsext_host_name(data->ssl, host);

        // This call will now block until the handshake is complete or fails.
        if (SSL_connect(data->ssl) != 1) {
            char final_error_msg[512];
            char ssl_error_buf[256];
            ERR_error_string_n(ERR_get_error(), ssl_error_buf, sizeof(ssl_error_buf));
            sprintf(final_error_msg, "SSL handshake failed: %s", ssl_error_buf);
            angara_throw_error(final_error_msg);

            angara_decref(self);
            return angara_create_nil();
        }
    }

    // --- 4. wslay Setup ---
    struct wslay_event_callbacks wslay_cbs = {
            wslay_recv_callback,
            wslay_send_callback,
            wslay_genmask_callback, // <-- Provide the required callback
            NULL,
            NULL,
            NULL,
            wslay_on_msg_recv_callback
    };
    wslay_event_context_client_init(&data->ctx, &wslay_cbs, data);

    // --- 5. WebSocket Handshake ---
    // 5a. Generate a random key
    unsigned char client_key_raw[16];
    RAND_bytes(client_key_raw, 16);
    char client_key_b64[32];
    EVP_EncodeBlock((unsigned char*)client_key_b64, client_key_raw, 16);

    // 5b. Build the HTTP Upgrade request
    char request_buf[1024];
    sprintf(request_buf, "GET %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Upgrade: websocket\r\n"
                         "Connection: Upgrade\r\n"
                         "Sec-WebSocket-Key: %s\r\n"
                         "Sec-WebSocket-Version: 13\r\n\r\n", path, host, client_key_b64);

    // 5c. Send the request
    if (use_ssl) SSL_write(data->ssl, request_buf, strlen(request_buf));
    else write(data->fd, request_buf, strlen(request_buf));

    // 5d. Read the server's response header.
    char response_buf[2048];
    int len;
    if (use_ssl) len = SSL_read(data->ssl, response_buf, sizeof(response_buf) - 1);
    else len = read(data->fd, response_buf, sizeof(response_buf) - 1);

    // Check for a REAL read error now.
    if (len <= 0) {
        angara_throw_error("Failed to read handshake response from server (connection closed).");
        angara_decref(self); return angara_create_nil();
    }
    response_buf[len] = '\0';

    // 5e. Validate the response status and headers.
    if (strcasestr(response_buf, "HTTP/1.1 101") == NULL ||
            strcasestr(response_buf, "Upgrade: websocket") == NULL ||
            strcasestr(response_buf, "Connection: Upgrade") == NULL) {

        angara_throw_error("WebSocket handshake failed: Invalid HTTP response.");
        angara_decref(self); return angara_create_nil();
    }

    // 5f. --- THE KEY VALIDATION LOGIC ---
    const char* accept_key_header = "Sec-WebSocket-Accept: ";
    char* server_accept_key = strcasestr(response_buf, accept_key_header);
    if (!server_accept_key) {

        angara_throw_error("WebSocket handshake failed: Server is missing Sec-WebSocket-Accept header.");
        angara_decref(self); return angara_create_nil();
    }
    server_accept_key += strlen(accept_key_header);
    char* eol = strcasestr(server_accept_key, "\r\n");
    if (eol) *eol = '\0'; // Null-terminate the key value.

    // Calculate the expected key.
    const char* magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char challenge[256];
    sprintf(challenge, "%s%s", client_key_b64, magic_string);

    unsigned char sha1_result[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)challenge, strlen(challenge), sha1_result);

    char* expected_accept_key = base64_encode(sha1_result, SHA_DIGEST_LENGTH);

    // Finally, compare the keys.
    if (strcmp(expected_accept_key, server_accept_key) != 0) {
        free(expected_accept_key);
        angara_throw_error("WebSocket handshake failed: Invalid Sec-WebSocket-Accept key.");
        angara_decref(self); return angara_create_nil();
    }
    free(expected_accept_key);
    // --- END VALIDATION ---

    fcntl(data->fd, F_SETFL, O_NONBLOCK);

    // --- 6. Handshake is successful, connection is ready ---
    data->is_connected = true;
    if (!IS_NIL(data->on_open_closure)) {
        angara_call(data->on_open_closure, 1, (AngaraObject[]){self});
    }


    return self;
}

// --- Methods ---
AngaraObject Angara_WebSocket_send(int arg_count, AngaraObject* args) {

    AngaraObject self = args[0];
    AngaraObject message = args[1];
    if (arg_count != 2 || !IS_NATIVE_INSTANCE(self) || !IS_STRING(message)) {
        return angara_create_nil();
    }

    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;

    if (!data->is_connected) {
        return angara_create_nil();
    }


    struct wslay_event_msg msg_to_queue;
    msg_to_queue.opcode = WSLAY_TEXT_FRAME;
    msg_to_queue.msg = (const uint8_t*)AS_CSTRING(message);
    msg_to_queue.msg_length = AS_STRING(message)->length;

    int result = wslay_event_queue_msg(data->ctx, &msg_to_queue);

    return angara_create_nil();
}

AngaraObject Angara_WebSocket_close(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;

    // Prevent double-closing
    if (!data->is_connected) {
        return angara_create_nil();
    }

    // --- THE FIX ---
    // 1. Immediately mark the connection as logically closed from our side.
    data->is_connected = false;
    data->should_close = true;

    // 2. Queue the WebSocket close frame to send to the server.
    wslay_event_queue_close(data->ctx, WSLAY_CODE_NORMAL_CLOSURE, "Client closing", 16);

    // 3. Attempt to send any queued frames (including our close frame) immediately.
    wslay_event_send(data->ctx);

    // 4. Trigger the user's on_close callback immediately.
    //    The user's intent to close should be honored now, not later.
    if (!IS_NIL(data->on_close_closure)) {
        angara_call(data->on_close_closure, 1, (AngaraObject[]){self});
    }

    return angara_create_nil();
}

AngaraObject Angara_WebSocket_service(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;

    // Guard against operating on a closed connection.
    if (!data->is_connected || data->should_close || !data->ctx) {
        return angara_create_nil();
    }

    int err = 0;

    // Try to send any queued data (e.g., the message from ws.send or a close frame).
    if (wslay_event_want_write(data->ctx)) {
        err = wslay_event_send(data->ctx);
    }

    // If sending was successful, try to receive data.
    if (err == 0 && wslay_event_want_read(data->ctx)) {
        err = wslay_event_recv(data->ctx);
    }

    // --- THE FIX IS HERE ---
    // After ANY I/O operation, check if an error occurred.
    // A non-zero error code from wslay means the connection is now closed,
    // either because we initiated it, the peer did, or an error occurred.
    if (err != 0) {
        // Only trigger the on_close callback if we haven't already.
        if (data->is_connected) {
            data->is_connected = false; // Mark as disconnected.

            // It's a clean close, not an error, if we intended to close.
            if (!IS_NIL(data->on_close_closure) && data->should_close) {
                angara_call(data->on_close_closure, 1, (AngaraObject[]){self});
            }
                // Handle unexpected errors if an on_error handler exists.
            else if (!IS_NIL(data->on_error_closure)) {
                AngaraObject err_msg = angara_create_string("Connection closed unexpectedly.");
                angara_call(data->on_error_closure, 2, (AngaraObject[]){ self, err_msg });
                angara_decref(err_msg);
            }
        }
    }
    return angara_create_nil();
}

AngaraObject Angara_WebSocket_is_open(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    WebSocketData* data = (WebSocketData*)AS_NATIVE_INSTANCE(self)->data;
    return angara_create_bool(data->is_connected && !data->should_close);
}

// --- ABI Definitions ---
static const AngaraMethodDef WEBSOCKET_METHODS[] = {
        {"send",     (AngaraMethodFn)Angara_WebSocket_send,     "s->n"},
        {"close",    (AngaraMethodFn)Angara_WebSocket_close,    "->n"},
        {"service",  (AngaraMethodFn)Angara_WebSocket_service,  "->n"},
        {"is_open",  (AngaraMethodFn)Angara_WebSocket_is_open,  "->b"},
        {NULL, NULL, NULL}
};

static const AngaraClassDef WEBSOCKET_CLASS_DEF = { "WebSocket", NULL, WEBSOCKET_METHODS };

static const AngaraFuncDef WEBSOCKET_EXPORTS[] = {
        { "connect", Angara_websocket_connect, "s{}->WebSocket", &WEBSOCKET_CLASS_DEF },
        { NULL, NULL, NULL, NULL }
};

ANGARA_MODULE_INIT(websocket) {
        *def_count = (sizeof(WEBSOCKET_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
        return WEBSOCKET_EXPORTS;
}
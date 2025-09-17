#include "../src/runtime/angara_runtime.h" // Your provided ABI header
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// --- Private Data Structures ---
typedef struct {
    amqp_connection_state_t conn;
    bool is_open;
    amqp_channel_t next_channel_id;
} ConnectionData;

typedef struct {
    AngaraObject conn_obj; // A handle back to the parent Connection object
    amqp_channel_t id;
    bool is_open;
} ChannelData;

// --- Helpers ---
static inline ChannelData* get_channel_data(AngaraObject self) {
    return (ChannelData*)AS_NATIVE_INSTANCE(self)->data;
}
static inline ConnectionData* get_conn_data_from_channel(ChannelData* ch_data) {
    return (ConnectionData*)AS_NATIVE_INSTANCE(ch_data->conn_obj)->data;
}

static void check_amqp_reply(amqp_rpc_reply_t reply, const char* context) {
    if (reply.reply_type == AMQP_RESPONSE_NORMAL) return;

    char err_buf[512];
    if (reply.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION) {
        amqp_method_t *method = (amqp_method_t *)reply.reply.decoded;
        if (method->id == AMQP_CONNECTION_CLOSE_METHOD) {
            amqp_connection_close_t *m = (amqp_connection_close_t *)method->decoded;
            sprintf(err_buf, "%s: server connection error %d, message: %.*s",
                    context, m->reply_code, (int)m->reply_text.len, (char *)m->reply_text.bytes);
        } else if (method->id == AMQP_CHANNEL_CLOSE_METHOD) {
            amqp_channel_close_t *m = (amqp_channel_close_t *)method->decoded;
            sprintf(err_buf, "%s: server channel error %d, message: %.*s",
                    context, m->reply_code, (int)m->reply_text.len, (char *)m->reply_text.bytes);
        } else {
            sprintf(err_buf, "%s: unexpected server exception, method id 0x%08X", context, method->id);
        }
    } else if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
        sprintf(err_buf, "%s: %s", context, amqp_error_string2(reply.library_error));
    }
    angara_throw_error(err_buf);
}

// --- Finalizers ---
void finalize_connection(void* data_ptr) {
    ConnectionData* data = (ConnectionData*)data_ptr;
    angara_debug_print("Finalizing AMQP Connection.");
    if (data->is_open) {
        amqp_connection_close(data->conn, AMQP_REPLY_SUCCESS);
    }
    amqp_destroy_connection(data->conn);
    free(data);
}

void finalize_channel(void* data_ptr) {
    ChannelData* data = (ChannelData*)data_ptr;
    angara_debug_print("Finalizing AMQP Channel.");
    angara_decref(data->conn_obj);
    free(data);
}


// --- CONNECTION CLASS ---

// CONSTRUCTOR: `amqp.connect(url as string) -> Connection`
AngaraObject Angara_amqp_connect(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("connect(url) expects one string argument.");
        return angara_create_nil();
    }
    const char* url = AS_CSTRING(args[0]);

    ConnectionData* data = (ConnectionData*)malloc(sizeof(ConnectionData));
    data->is_open = false;
    data->next_channel_id = 1;

    char user[128] = "guest", password[128] = "guest", host[256] = "localhost";
    int port = 5672;
    sscanf(url, "amqp://%127[^:]:%127[^@]@%255[^:]:%d", user, password, host, &port);

    data->conn = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(data->conn);
    if (!socket || amqp_socket_open(socket, host, port) < 0) {
        amqp_destroy_connection(data->conn); free(data);
        angara_throw_error("Failed to open TCP socket to AMQP broker.");
        return angara_create_nil();
    }
    check_amqp_reply(amqp_login(data->conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, user, password), "Logging in");
    data->is_open = true;
    return angara_create_native_instance(data, finalize_connection);
}

// Method: conn.channel() -> Channel
AngaraObject Angara_Connection_channel(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    ConnectionData* conn_data = (ConnectionData*)AS_NATIVE_INSTANCE(self)->data;
    if (!conn_data->is_open) {
        angara_throw_error("Cannot open a channel on a closed connection.");
        return angara_create_nil();
    }
    ChannelData* ch_data = (ChannelData*)malloc(sizeof(ChannelData));
    ch_data->conn_obj = self;
    angara_incref(self);
    ch_data->id = conn_data->next_channel_id++;
    amqp_channel_open(conn_data->conn, ch_data->id);
    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Opening channel");
    ch_data->is_open = true;
    return angara_create_native_instance(ch_data, finalize_channel);
}

// METHOD: `conn.close()`
AngaraObject Angara_Connection_close(int arg_count, AngaraObject* args) {
    ConnectionData* data = (ConnectionData*)AS_NATIVE_INSTANCE(args[0])->data;
    if (data->is_open) {
        check_amqp_reply(amqp_connection_close(data->conn, AMQP_REPLY_SUCCESS), "Closing connection");
        data->is_open = false;
    }
    return angara_create_nil();
}


// --- CHANNEL CLASS ---

// METHOD: `ch.queue_declare(name as string, durable=false, exclusive=false, auto_delete=false)`
AngaraObject Angara_Channel_queue_declare(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || arg_count > 5 || !IS_STRING(args[1])) {
        angara_throw_error("queue_declare(name, [durable], [exclusive], [auto_delete]) invalid arguments.");
        return angara_create_nil();
    }
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);

    const char* queue_name = AS_CSTRING(args[1]);
    amqp_boolean_t durable = (arg_count > 2 && angara_is_truthy(args[2])) ? 1 : 0;
    amqp_boolean_t exclusive = (arg_count > 3 && angara_is_truthy(args[3])) ? 1 : 0;
    amqp_boolean_t auto_delete = (arg_count > 4 && angara_is_truthy(args[4])) ? 1 : 0;

    amqp_queue_declare(conn_data->conn, ch_data->id, amqp_cstring_bytes(queue_name),
                       0, durable, exclusive, auto_delete, amqp_empty_table);
    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Declaring queue");
    return angara_create_nil();
}

// METHOD: `ch.publish(exchange as string, routing_key as string, body as string)`
AngaraObject Angara_Channel_publish(int arg_count, AngaraObject* args) {
    if (arg_count != 4 || !IS_STRING(args[1]) || !IS_STRING(args[2]) || !IS_STRING(args[3])) {
        angara_throw_error("publish(exchange, routing_key, body) expects three string arguments.");
        return angara_create_nil();
    }
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);
    const char* exchange = AS_CSTRING(args[1]);
    const char* routing_key = AS_CSTRING(args[2]);
    const char* body = AS_CSTRING(args[3]);

    amqp_basic_publish(conn_data->conn, ch_data->id,
                       amqp_cstring_bytes(exchange), amqp_cstring_bytes(routing_key),
                       0, 0, NULL, amqp_cstring_bytes(body));
    return angara_create_nil();
}

// *** NEW METHOD: `ch.consume(queue as string, [timeout_ms as i64]) -> record|nil` ***
AngaraObject Angara_Channel_consume(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || arg_count > 3 || !IS_STRING(args[1])) {
        angara_throw_error("consume(queue, [timeout_ms]) invalid arguments.");
        return angara_create_nil();
    }
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);
    const char* queue = AS_CSTRING(args[1]);

    // Register this channel as a consumer for the queue. no_ack is set to false.
    amqp_basic_consume(conn_data->conn, ch_data->id, amqp_cstring_bytes(queue),
                       amqp_empty_bytes, 0, 0, 0, amqp_empty_table);
    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Starting consumer");

    // Block and wait for a single message.
    amqp_envelope_t envelope;
    struct timeval* timeout = NULL;
    struct timeval tv;
    if (arg_count == 3 && IS_I64(args[2])) {
        int64_t ms = AS_I64(args[2]);
        if (ms >= 0) {
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            timeout = &tv;
        }
    }

    amqp_rpc_reply_t res = amqp_consume_message(conn_data->conn, &envelope, timeout, 0);

    if (res.reply_type != AMQP_RESPONSE_NORMAL) {
        amqp_destroy_envelope(&envelope);
        if (res.library_error == AMQP_STATUS_TIMEOUT) {
            return angara_create_nil(); // Timeout is not an error, just return nil.
        }
        check_amqp_reply(res, "Consuming message");
        return angara_create_nil(); // Should not be reached
    }

    // Copy the message body into a new Angara string.
    AngaraObject body_str = angara_create_string_no_copy(
            (char*)envelope.message.body.bytes, envelope.message.body.len);
    AngaraObject delivery_tag = angara_create_i64(envelope.delivery_tag);

    // Create the result record { body: "...", delivery_tag: 123 }
    AngaraObject result = angara_record_new_with_fields(2, (AngaraObject[]){
            angara_create_string("body"), body_str,
            angara_create_string("delivery_tag"), delivery_tag,
    });

    angara_decref(body_str);
    angara_decref(delivery_tag);

    amqp_destroy_envelope(&envelope); // VERY IMPORTANT: Free the library's memory.
    return result;
}

// *** NEW METHOD: `ch.ack(delivery_tag as i64)` ***
AngaraObject Angara_Channel_ack(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_I64(args[1])) {
        angara_throw_error("ack(delivery_tag) expects one integer argument.");
        return angara_create_nil();
    }
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);
    uint64_t delivery_tag = (uint64_t)AS_I64(args[1]);

    // Acknowledge a single message.
    amqp_basic_ack(conn_data->conn, ch_data->id, delivery_tag, 0);
    return angara_create_nil();
}

// METHOD: `ch.close()`
AngaraObject Angara_Channel_close(int arg_count, AngaraObject* args) {
    ChannelData* data = get_channel_data(args[0]);
    if (data->is_open) {
        ConnectionData* conn_data = get_conn_data_from_channel(data);
        check_amqp_reply(amqp_channel_close(conn_data->conn, data->id, AMQP_REPLY_SUCCESS), "Closing channel");
        data->is_open = false;
    }
    return angara_create_nil();
}


// --- ABI Definitions ---

// --- Dummy Function for Class Registration ---
// This function is never meant to be called by users. Its sole purpose
// is to be present in the export list so that the Angara runtime can
// learn about the Channel class via the `constructs` field.
AngaraObject Angara_amqp_dummy_channel_ctor(__attribute__((unused)) int arg_count, __attribute__((unused)) AngaraObject* args) {
    angara_throw_error("The _channel constructor is private and cannot be called directly.");
    return angara_create_nil();
}

static const AngaraMethodDef CHANNEL_METHODS[] = {
        {"queue_declare", (AngaraMethodFn)Angara_Channel_queue_declare, "sbbb?->n"},
        {"publish",       (AngaraMethodFn)Angara_Channel_publish,       "sss->n"},
        {"consume",       (AngaraMethodFn)Angara_Channel_consume,       "si?->{}?"},
        {"ack",           (AngaraMethodFn)Angara_Channel_ack,           "i->n"},
        {"close",         (AngaraMethodFn)Angara_Channel_close,         "->n"},
        {NULL, NULL, NULL}
};
const AngaraClassDef CHANNEL_CLASS_DEF = { "Channel", NULL, CHANNEL_METHODS };

static const AngaraMethodDef CONNECTION_METHODS[] = {
        {"channel", (AngaraMethodFn)Angara_Connection_channel, "->Channel"},
        {"close",   (AngaraMethodFn)Angara_Connection_close,   "->n"},
        {NULL, NULL, NULL}
};
const AngaraClassDef CONNECTION_CLASS_DEF = { "Connection", NULL, CONNECTION_METHODS };

const AngaraFuncDef AMQP_EXPORTS[] = {
        { "connect", Angara_amqp_connect, "s->Connection", &CONNECTION_CLASS_DEF },
        // This "private" export registers the Channel class with the runtime.
        { "_channel", Angara_amqp_dummy_channel_ctor, "->Channel", &CHANNEL_CLASS_DEF },
        { NULL, NULL, NULL, NULL }
};

ANGARA_MODULE_INIT(amqp) {
    *def_count = (sizeof(AMQP_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return AMQP_EXPORTS;
}
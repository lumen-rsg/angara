#include "../runtime/angara_runtime.h" // Your provided ABI header
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEBUG false

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
    if (DEBUG) fprintf(stderr, "[DEBUG] queue_declare called.\n"); // <-- DEBUG

    if (arg_count < 2 || arg_count > 5 || !IS_STRING(args[1])) {
        angara_throw_error("queue_declare(name, [durable], [exclusive], [auto_delete]) invalid arguments.");
        return angara_create_nil();
    }
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);

    if (DEBUG) fprintf(stderr, "[DEBUG] QD Channel ID: %d\n", ch_data->id); // <-- DEBUG

    const char* queue_name = AS_CSTRING(args[1]);
    if (DEBUG) fprintf(stderr, "[DEBUG] QD Queue Name: '%s'\n", queue_name); // <-- DEBUG

    amqp_boolean_t durable = (arg_count > 2 && angara_is_truthy(args[2])) ? 1 : 0;
    amqp_boolean_t exclusive = (arg_count > 3 && angara_is_truthy(args[3])) ? 1 : 0;
    amqp_boolean_t auto_delete = (arg_count > 4 && angara_is_truthy(args[4])) ? 1 : 0;

    if (DEBUG) fprintf(stderr, "[DEBUG] Calling amqp_queue_declare...\n"); // <-- DEBUG

    // Note: We pass 0 for passive.
    amqp_queue_declare_ok_t *r = amqp_queue_declare(conn_data->conn, ch_data->id, amqp_cstring_bytes(queue_name),
                       0, durable, exclusive, auto_delete, amqp_empty_table);

    if (DEBUG) fprintf(stderr, "[DEBUG] amqp_queue_declare returned. Checking reply...\n"); // <-- DEBUG

    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Declaring queue");

    if (DEBUG) fprintf(stderr, "[DEBUG] queue_declare success.\n"); // <-- DEBUG
    return angara_create_nil();
}

// METHOD: `ch.publish(exchange as string, routing_key as string, body as string)`
// METHOD: `ch.publish(exchange as string, routing_key as string, body as string, [options as record?])`
AngaraObject Angara_Channel_publish(int arg_count, AngaraObject* args) {
    // We now accept 4 or 5 arguments (self, exchange, key, body, [options])
    if (arg_count < 4 || arg_count > 5 || !IS_STRING(args[1]) || !IS_STRING(args[2]) || !IS_STRING(args[3])) {
        angara_throw_error("publish(exchange, routing_key, body, [options]) has invalid base arguments.");
        return angara_create_nil();
    }

    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);
    const char* exchange = AS_CSTRING(args[1]);
    const char* routing_key = AS_CSTRING(args[2]);
    const char* body = AS_CSTRING(args[3]);
    amqp_bytes_t body_bytes = amqp_cstring_bytes(body);

    // --- NEW: Handle Optional Properties ---
    amqp_basic_properties_t props;
    props._flags = 0; // Start with no properties set

    if (arg_count == 5 && IS_RECORD(args[4])) {
        AngaraObject options = args[4];

        // Check for correlationId
        AngaraObject corr_id_obj = angara_record_get(options, "correlationId");
        if (IS_STRING(corr_id_obj)) {
            props._flags |= AMQP_BASIC_CORRELATION_ID_FLAG;
            props.correlation_id = amqp_cstring_bytes(AS_CSTRING(corr_id_obj));
        }

        // Check for replyTo
        AngaraObject reply_to_obj = angara_record_get(options, "replyTo");
        if (IS_STRING(reply_to_obj)) {
            props._flags |= AMQP_BASIC_REPLY_TO_FLAG;
            props.reply_to = amqp_cstring_bytes(AS_CSTRING(reply_to_obj));
        }

        // Decref the objects we retrieved from the record
        angara_decref(corr_id_obj);
        angara_decref(reply_to_obj);
    }
    // --- END NEW ---

    // The final publish call now includes the properties.
    amqp_basic_publish(conn_data->conn, ch_data->id,
                       amqp_cstring_bytes(exchange),
                       amqp_cstring_bytes(routing_key),
                       0, 0, &props, body_bytes);

    return angara_create_nil();
}

AngaraObject Angara_Channel_subscribe(int arg_count, AngaraObject* args) {
    if (DEBUG) fprintf(stderr, "[DEBUG] subscribe called.\n"); // <-- DEBUG

    if (arg_count != 2 || !IS_STRING(args[1])) {
        angara_throw_error("subscribe(queue) expects one string argument.");
        return angara_create_nil();
    }
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);
    const char* queue = AS_CSTRING(args[1]);

    if (DEBUG) fprintf(stderr, "[DEBUG] Subscribing to queue '%s' on Channel %d\n", queue, ch_data->id); // <-- DEBUG

    amqp_basic_consume(conn_data->conn, ch_data->id, amqp_cstring_bytes(queue),
                       amqp_empty_bytes, 0, 0, 0, amqp_empty_table);

    if (DEBUG) fprintf(stderr, "[DEBUG] amqp_basic_consume sent. Waiting for RPC reply...\n"); // <-- DEBUG

    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Subscribing to queue");

    if (DEBUG) fprintf(stderr, "[DEBUG] subscribe success.\n"); // <-- DEBUG
    return angara_create_nil();
}

// METHOD: `ch.next_message(timeout_ms as i64?) -> record?`
AngaraObject Angara_Channel_next_message(int arg_count, AngaraObject* args) {
    if (DEBUG) fprintf(stderr, "[DEBUG] next_message called.\n"); // <-- DEBUG

    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);

    // Timeout logic
    struct timeval* timeout = NULL;
    struct timeval tv;
    if (arg_count == 2 && IS_I64(args[1])) {
        int64_t ms = AS_I64(args[1]);
        if (ms >= 0) {
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            timeout = &tv;
        }
    }

    if (DEBUG) fprintf(stderr, "[DEBUG] Calling amqp_consume_message on Channel %d...\n", ch_data->id); // <-- DEBUG

    amqp_envelope_t envelope;
    amqp_rpc_reply_t res = amqp_consume_message(conn_data->conn, &envelope, timeout, 0);

    if (DEBUG) fprintf(stderr, "[DEBUG] amqp_consume_message returned type: %d\n", res.reply_type); // <-- DEBUG

    if (res.reply_type != AMQP_RESPONSE_NORMAL) {
        if (res.library_error == AMQP_STATUS_TIMEOUT) {
            if (DEBUG) fprintf(stderr, "[DEBUG] Timeout.\n"); // <-- DEBUG
            return angara_create_nil();
        }

        // --- FIX: DO NOT DESTROY ENVELOPE HERE ---
        // The envelope is only valid if reply_type is NORMAL.
        // Destroying it here causes a SEGFAULT.

        if (DEBUG) fprintf(stderr, "[DEBUG] Consume error library_error: %d\n", res.library_error); // <-- DEBUG

        // We should probably throw here to let the user know,
        // but for now let's just return nil and log to stderr to see what's happening.
        // check_amqp_reply(res, "Consuming message"); // This throws.

        // Let's throw safely
        char err_buf[512];
        if (res.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
             sprintf(err_buf, "Consumer library error: %s", amqp_error_string2(res.library_error));
        } else if (res.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION) {
             sprintf(err_buf, "Consumer server exception");
        } else {
             sprintf(err_buf, "Consumer unknown error");
        }
        angara_throw_error(err_buf);

        return angara_create_nil();
    }

    if (DEBUG) fprintf(stderr, "[DEBUG] Message received! Processing...\n"); // <-- DEBUG

    // --- Construct the Result Record ---
    AngaraObject result = angara_record_new();

    AngaraObject body_str = angara_create_string_with_len(
        (const char*)envelope.message.body.bytes, envelope.message.body.len);
    angara_record_set(result, "body", body_str);
    angara_decref(body_str);

    AngaraObject delivery_tag = angara_create_i64(envelope.delivery_tag);
    angara_record_set(result, "delivery_tag", delivery_tag);
    angara_decref(delivery_tag);

    if (envelope.message.properties._flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
        AngaraObject corr_id_str = angara_create_string_with_len(
            (const char*)envelope.message.properties.correlation_id.bytes,
            envelope.message.properties.correlation_id.len);
        angara_record_set(result, "correlationId", corr_id_str);
        angara_decref(corr_id_str);
    }

    if (envelope.message.properties._flags & AMQP_BASIC_REPLY_TO_FLAG) {
        AngaraObject reply_to_str = angara_create_string_with_len(
            (const char*)envelope.message.properties.reply_to.bytes,
            envelope.message.properties.reply_to.len);
        angara_record_set(result, "replyTo", reply_to_str);
        angara_decref(reply_to_str);
    }

    amqp_destroy_envelope(&envelope);
    if (DEBUG) fprintf(stderr, "[DEBUG] next_message success.\n"); // <-- DEBUG
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

AngaraObject Angara_Channel_nack(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_I64(args[1]) || !IS_BOOL(args[2])) {
        angara_throw_error("nack(delivery_tag, requeue) expects an integer and a boolean.");
        return angara_create_nil();
    }
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);

    uint64_t delivery_tag = (uint64_t)AS_I64(args[1]);
    amqp_boolean_t requeue = AS_BOOL(args[2]);

    // Acknowledge a single message negatively. `multiple` flag is false.
    amqp_basic_nack(conn_data->conn, ch_data->id, delivery_tag, 0, requeue);

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

AngaraObject Angara_Channel_bind_queue(int arg_count, AngaraObject* args) {
    if (DEBUG) fprintf(stderr, "[DEBUG] bind_queue called.\n"); // <-- DEBUG

    if (arg_count != 4 || !IS_STRING(args[1]) || !IS_STRING(args[2]) || !IS_STRING(args[3])) {
        angara_throw_error("bind_queue(queue, exchange, routing_key) expects three string arguments.");
        return angara_create_nil();
    }

    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);

    const char* queue_name = AS_CSTRING(args[1]);
    const char* exchange_name = AS_CSTRING(args[2]);
    const char* routing_key = AS_CSTRING(args[3]);

    if (DEBUG) fprintf(stderr, "[DEBUG] Binding Q '%s' to E '%s' key '%s'\n", queue_name, exchange_name, routing_key); // <-- DEBUG

    amqp_queue_bind(conn_data->conn, ch_data->id,
                    amqp_cstring_bytes(queue_name),
                    amqp_cstring_bytes(exchange_name),
                    amqp_cstring_bytes(routing_key),
                    amqp_empty_table);

    if (DEBUG) fprintf(stderr, "[DEBUG] bind_queue reply check...\n"); // <-- DEBUG
    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Binding queue");

    if (DEBUG) fprintf(stderr, "[DEBUG] bind_queue success.\n"); // <-- DEBUG

    return angara_create_nil();
}

AngaraObject Angara_Channel_exchange_declare(int arg_count, AngaraObject* args) {
    if (DEBUG) fprintf(stderr, "[DEBUG] exchange_declare called. arg_count=%d\n", arg_count); // <-- DEBUG

    if (arg_count < 3 || arg_count > 6 || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        angara_throw_error("exchange_declare(name, type, ...) invalid arguments.");
        return angara_create_nil();
    }

    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);

    if (DEBUG) fprintf(stderr, "[DEBUG] Channel ID: %d, Connection Ptr: %p\n", ch_data->id, (void*)conn_data->conn); // <-- DEBUG

    const char* exchange_name = AS_CSTRING(args[1]);
    const char* exchange_type = AS_CSTRING(args[2]);

    if (DEBUG) fprintf(stderr, "[DEBUG] Declaring exchange '%s' type '%s'\n", exchange_name, exchange_type); // <-- DEBUG

    amqp_boolean_t passive = (arg_count > 3 && angara_is_truthy(args[3])) ? 1 : 0;
    amqp_boolean_t durable = (arg_count > 4 && angara_is_truthy(args[4])) ? 1 : 0;
    amqp_boolean_t auto_delete = (arg_count > 5 && angara_is_truthy(args[5])) ? 1 : 0;

    if (DEBUG) fprintf(stderr, "[DEBUG] Calling amqp_exchange_declare...\n"); // <-- DEBUG

    amqp_exchange_declare(conn_data->conn, ch_data->id,
                          amqp_cstring_bytes(exchange_name),
                          amqp_cstring_bytes(exchange_type),
                          passive, durable, auto_delete, 0, amqp_empty_table);

    if (DEBUG) fprintf(stderr, "[DEBUG] amqp_exchange_declare returned. Checking reply...\n"); // <-- DEBUG

    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Declaring exchange");

    if (DEBUG) fprintf(stderr, "[DEBUG] exchange_declare success.\n"); // <-- DEBUG

    return angara_create_nil();
}

AngaraObject Angara_Channel_prefetch(int arg_count, AngaraObject* args) {
    if (DEBUG) fprintf(stderr, "[DEBUG] prefetch called.\n"); // <-- DEBUG

    if (arg_count != 2 || !IS_I64(args[1])) {
        angara_throw_error("prefetch(count) expects one integer argument.");
        return angara_create_nil();
    }

    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);
    uint16_t prefetch_count = (uint16_t)AS_I64(args[1]);

    if (DEBUG) fprintf(stderr, "[DEBUG] Setting prefetch to %d on Channel %d\n", prefetch_count, ch_data->id); // <-- DEBUG

    // amqp_basic_qos sends the Qos method. It does NOT block waiting for QosOk.
    amqp_basic_qos(conn_data->conn, ch_data->id, 0, prefetch_count, 0);

    if (DEBUG) fprintf(stderr, "[DEBUG] amqp_basic_qos sent. Waiting for RPC reply...\n"); // <-- DEBUG

    // We must manually wait for the QosOk method frame to ensure the server accepted it.
    amqp_rpc_reply_t res = amqp_get_rpc_reply(conn_data->conn);

    if (DEBUG) fprintf(stderr, "[DEBUG] RPC reply received. Checking status...\n"); // <-- DEBUG

    check_amqp_reply(res, "Setting prefetch (QOS)");

    if (DEBUG) fprintf(stderr, "[DEBUG] prefetch success.\n"); // <-- DEBUG
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
        {"exchange_declare", (AngaraMethodFn)Angara_Channel_exchange_declare, "ssbbb?->n"},
        {"publish",       (AngaraMethodFn)Angara_Channel_publish,       "sss{}?->n"},
        {"subscribe",    (AngaraMethodFn)Angara_Channel_subscribe,    "s->n"},
        {"next_message", (AngaraMethodFn)Angara_Channel_next_message, "i?->{}?"},
        {"ack",           (AngaraMethodFn)Angara_Channel_ack,           "i->n"},
        {"close",         (AngaraMethodFn)Angara_Channel_close,         "->n"},
        {"bind_queue",    (AngaraMethodFn)Angara_Channel_bind_queue,    "sss->n"},
        {"prefetch",      (AngaraMethodFn)Angara_Channel_prefetch,      "i->n"},
        {"nack",          (AngaraMethodFn)Angara_Channel_nack,          "ib->n"},
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
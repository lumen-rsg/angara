#include "../runtime/angara_runtime.h" // Your provided ABI header
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

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
    angara_create_native_instance(data, finalize_connection, "Connection");
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
    angara_create_native_instance(ch_data, finalize_channel, "Channel");
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

    // Optional flags
    int durable = (arg_count > 2 && angara_is_truthy(args[2])) ? 1 : 0;
    int exclusive = (arg_count > 3 && angara_is_truthy(args[3])) ? 1 : 0;
    int auto_delete = (arg_count > 4 && angara_is_truthy(args[4])) ? 1 : 0;

    // Perform the declaration
    // We capture the return value 'r' which contains the actual generated queue name
    amqp_queue_declare_ok_t *r = amqp_queue_declare(
        conn_data->conn,
        ch_data->id,
        amqp_cstring_bytes(queue_name),
        0, // passive
        durable,
        exclusive,
        auto_delete,
        amqp_empty_table
    );

    // Check for RPC errors
    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Declaring queue");

    // If we are here, success. Construct the result record.

    // 1. Convert the returned queue name (bytes) to Angara String
    // Note: r->queue is NOT null-terminated, so we must use length.
    AngaraObject real_name_obj;
    if (r->queue.len > 0) {
        real_name_obj = angara_create_string_with_len((char*)r->queue.bytes, r->queue.len);
    } else {
        real_name_obj = angara_string_from_c("");
    }

    // 2. Prepare keys and values for the record
    AngaraObject k_queue = angara_string_from_c("queue");
    AngaraObject k_msgs  = angara_string_from_c("message_count");
    AngaraObject k_cons  = angara_string_from_c("consumer_count");

    AngaraObject v_msgs  = angara_create_i64(r->message_count);
    AngaraObject v_cons  = angara_create_i64(r->consumer_count);

    AngaraObject kvs[] = {
        k_queue, real_name_obj,
        k_msgs,  v_msgs,
        k_cons,  v_cons
    };

    // 3. Create the record
    AngaraObject record = angara_record_new_with_fields(3, kvs);

    // 4. Cleanup local references
    // (The record has incref'd the values and copied the keys, so we release our ownership)
    angara_decref(k_queue); angara_decref(real_name_obj);
    angara_decref(k_msgs);  angara_decref(v_msgs);
    angara_decref(k_cons);  angara_decref(v_cons);

    return record;
}

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

AngaraObject Angara_Channel_next_message(int arg_count, AngaraObject* args) {
    ChannelData* ch_data = get_channel_data(args[0]);
    ConnectionData* conn_data = get_conn_data_from_channel(ch_data);

    struct timeval timeout;
    struct timeval* timeout_ptr = NULL;

    // 1. Parse Timeout
    // If arg provided, set up the struct. If NULL or < 0, timeout_ptr remains NULL (Block Forever).
    if (arg_count == 2 && IS_I64(args[1])) {
        int64_t ms = AS_I64(args[1]);
        if (ms >= 0) {
            timeout.tv_sec = ms / 1000;
            timeout.tv_usec = (ms % 1000) * 1000;
            timeout_ptr = &timeout;
        }
    }

    amqp_rpc_reply_t res;
    amqp_envelope_t envelope;

    amqp_maybe_release_buffers(conn_data->conn);

    // 2. Consume
    res = amqp_consume_message(conn_data->conn, &envelope, timeout_ptr, 0);

    // 3. Handle Result
    if (AMQP_RESPONSE_NORMAL != res.reply_type) {
        // If it was just a timeout, return nil (no message)
        if (AMQP_RESPONSE_LIBRARY_EXCEPTION == res.reply_type &&
            AMQP_STATUS_TIMEOUT == res.library_error) {
            return angara_create_nil();
        }

        // If connection closed or other error, throw
        // (Optional: You might want to return nil and let the user check is_open,
        // but for now throwing ensures we don't spin-loop on a dead connection)
        // char err_buf[256];
        // snprintf(err_buf, 256, "AMQP Consume Error: %s", amqp_error_string2(res.library_error));
        // angara_throw_error(err_buf);
        return angara_create_nil();
    }

    // 4. Construct Angara Record from Envelope
    // Body
    AngaraObject body_obj = angara_create_string_with_len((char*)envelope.message.body.bytes, envelope.message.body.len);

    // Properties (CorrelationId, ReplyTo)
    // Note: We need to handle cases where properties aren't set
    AngaraObject cid_obj = angara_create_nil();
    if (envelope.message.properties._flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
        cid_obj = angara_create_string_with_len(
            (char*)envelope.message.properties.correlation_id.bytes,
            envelope.message.properties.correlation_id.len
        );
    }

    AngaraObject reply_obj = angara_create_nil();
    if (envelope.message.properties._flags & AMQP_BASIC_REPLY_TO_FLAG) {
        reply_obj = angara_create_string_with_len(
            (char*)envelope.message.properties.reply_to.bytes,
            envelope.message.properties.reply_to.len
        );
    }

    // Delivery Tag
    AngaraObject dtag_obj = angara_create_i64((int64_t)envelope.delivery_tag);

    // Build "properties" record (Legacy support for your previous code)
    // or flatten it. Your Director code expects msg["correlationId"] directly?
    // Let's check director_service.an...
    // It uses: msg["correlationId"], msg["delivery_tag"], msg["body"], msg["replyTo"]
    // It does NOT use a nested "properties" object anymore in the latest fixes.

    AngaraObject k_body = angara_string_from_c("body");
    AngaraObject k_dtag = angara_string_from_c("delivery_tag");
    AngaraObject k_cid  = angara_string_from_c("correlationId");
    AngaraObject k_rep  = angara_string_from_c("replyTo");

    AngaraObject kvs[] = {
        k_body, body_obj,
        k_dtag, dtag_obj,
        k_cid,  cid_obj,
        k_rep,  reply_obj
    };

    AngaraObject record = angara_record_new_with_fields(4, kvs);

    // Cleanup
    angara_decref(k_body); angara_decref(body_obj);
    angara_decref(k_dtag); angara_decref(dtag_obj);
    angara_decref(k_cid);  angara_decref(cid_obj);
    angara_decref(k_rep);  angara_decref(reply_obj);

    amqp_destroy_envelope(&envelope);
    return record;
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
        {"queue_declare", (AngaraMethodFn)Angara_Channel_queue_declare, "sbbb?->{}"},
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
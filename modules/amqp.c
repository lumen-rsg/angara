#include "../runtime/angara_runtime.h"
#include <rabbitmq-c/tcp_socket.h>
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/framing.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#define DEBUG_AMQP 0

void dbg_amqp(const char* func, const char* msg) {
    if (DEBUG_AMQP) fprintf(stderr, "[AMQP DEBUG] %s: %s\n", func, msg);
}

// --- Data Structures ---

typedef struct {
    amqp_connection_state_t conn;
    amqp_socket_t *socket;
    int channel_count;
    bool is_connected;
} ConnectionData;

typedef struct {
    int id;
    AngaraObject connection_obj; // Strong ref to parent connection
    ConnectionData* conn_data;   // Cached raw pointer for speed
} ChannelData;

// --- Finalizers ---

void finalize_connection(void* data) {
    dbg_amqp("finalize_connection", "Destroying connection");
    ConnectionData* c = (ConnectionData*)data;
    if (c->is_connected) {
        amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS);
    }
    amqp_destroy_connection(c->conn);
    free(c);
}

void finalize_channel(void* data) {
    dbg_amqp("finalize_channel", "Destroying channel");
    ChannelData* ch = (ChannelData*)data;
    // Close channel if connection is still alive
    if (ch->conn_data->is_connected) {
        amqp_channel_close(ch->conn_data->conn, ch->id, AMQP_REPLY_SUCCESS);
    }
    // Release the parent connection
    angara_decref(ch->connection_obj);
    free(ch);
}

// --- Helpers ---

void check_amqp_reply(amqp_rpc_reply_t x, char const *context) {
    if (x.reply_type == AMQP_RESPONSE_NORMAL) return;

    char error_buf[512];
    if (x.reply_type == AMQP_RESPONSE_NONE) {
        snprintf(error_buf, 512, "%s: missing RPC reply type!", context);
    } else if (x.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
        snprintf(error_buf, 512, "%s: %s", context, amqp_error_string2(x.library_error));
    } else if (x.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION) {
        if (x.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
            amqp_connection_close_t *m = (amqp_connection_close_t *)x.reply.decoded;
            snprintf(error_buf, 512, "%s: Server Connection Error %d: %.*s", context, m->reply_code, (int)m->reply_text.len, (char *)m->reply_text.bytes);
        } else if (x.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
            amqp_channel_close_t *m = (amqp_channel_close_t *)x.reply.decoded;
            snprintf(error_buf, 512, "%s: Server Channel Error %d: %.*s", context, m->reply_code, (int)m->reply_text.len, (char *)m->reply_text.bytes);
        } else {
            snprintf(error_buf, 512, "%s: Unknown Server Exception", context);
        }
    }

    // Log and throw
    fprintf(stderr, "[AMQP ERROR] %s\n", error_buf);
    angara_throw_error(error_buf);
}

// --- Connection Methods ---

// connect(url: string) -> Connection
// connect(url: string) -> Connection
AngaraObject Angara_amqp_connect(int arg_count, AngaraObject* args) {
    dbg_amqp("connect", "Starting...");
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("connect(url) expects a string.");
        return angara_create_nil();
    }

    const char* url_str = AS_CSTRING(args[0]);
    struct amqp_connection_info ci;
    char* url_copy = strdup(url_str);

    if (amqp_parse_url(url_copy, &ci)) {
        free(url_copy);
        angara_throw_error("Malformed AMQP URL.");
        return angara_create_nil();
    }

    // --- FIX: Handle empty vhost from trailing slash ---
    // 'amqp://host/' parses as vhost="", but RabbitMQ needs "/"
    if (ci.vhost == NULL || *ci.vhost == '\0') {
        ci.vhost = "/";
    }
    // --------------------------------------------------

    ConnectionData* conn_data = (ConnectionData*)malloc(sizeof(ConnectionData));
    conn_data->conn = amqp_new_connection();
    conn_data->socket = amqp_tcp_socket_new(conn_data->conn);
    conn_data->channel_count = 0;
    conn_data->is_connected = false;

    if (!conn_data->socket) {
        free(url_copy); free(conn_data);
        angara_throw_error("Creating TCP socket failed.");
        return angara_create_nil();
    }

    if (amqp_socket_open(conn_data->socket, ci.host, ci.port)) {
        free(url_copy);
        amqp_destroy_connection(conn_data->conn); free(conn_data);
        angara_throw_error("Opening TCP socket failed.");
        return angara_create_nil();
    }

    amqp_rpc_reply_t reply = amqp_login(conn_data->conn, ci.vhost, 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, ci.user, ci.password);
    check_amqp_reply(reply, "Logging in");

    conn_data->is_connected = true;
    free(url_copy);

    dbg_amqp("connect", "Success");
    return angara_create_native_instance(conn_data, finalize_connection, "Connection");
}

// Connection.channel() -> Channel
AngaraObject Angara_Connection_channel(int arg_count, AngaraObject* args) {
    dbg_amqp("channel", "Creating new channel");
    // args[0] is the Connection instance
    AngaraObject conn_obj = args[0];
    ConnectionData* conn_data = (ConnectionData*)AS_NATIVE_INSTANCE(conn_obj)->data;

    // Allocate Channel Data
    ChannelData* ch_data = (ChannelData*)malloc(sizeof(ChannelData));
    ch_data->conn_data = conn_data;
    ch_data->connection_obj = conn_obj;

    // Assign ID (1-based)
    conn_data->channel_count++;
    ch_data->id = conn_data->channel_count;

    // Open Channel on Broker
    amqp_channel_open(conn_data->conn, ch_data->id);
    check_amqp_reply(amqp_get_rpc_reply(conn_data->conn), "Opening channel");

    // Keep Connection Alive!
    angara_incref(conn_obj);

    dbg_amqp("channel", "Channel created");
    return angara_create_native_instance(ch_data, finalize_channel, "Channel");
}

// Connection.close() -> nil
AngaraObject Angara_Connection_close(int arg_count, AngaraObject* args) {
    ConnectionData* c = (ConnectionData*)AS_NATIVE_INSTANCE(args[0])->data;
    if (c->is_connected) {
        amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS);
        c->is_connected = false;
    }
    return angara_create_nil();
}

// --- Channel Methods ---

// Helper to validate channel object
ChannelData* get_channel(AngaraObject obj) {
    return (ChannelData*)AS_NATIVE_INSTANCE(obj)->data;
}

// queue_declare(name, durable, exclusive, auto_delete) -> Record
AngaraObject Angara_Channel_queue_declare(int arg_count, AngaraObject* args) {
    dbg_amqp("queue_declare", "Called");
    ChannelData* ch = get_channel(args[0]);
    const char* queue = AS_CSTRING(args[1]);

    const char* queue_name = AS_CSTRING(args[1]);
    int passive = (arg_count > 2 && angara_is_truthy(args[2])) ? 1 : 0;
    int durable = (arg_count > 3 && angara_is_truthy(args[3])) ? 1 : 0;
    int exclusive = (arg_count > 4 && angara_is_truthy(args[4])) ? 1 : 0;
    int auto_delete = (arg_count > 5 && angara_is_truthy(args[5])) ? 1 : 0;

    amqp_queue_declare_ok_t *r = amqp_queue_declare(
        ch->conn_data->conn,
        ch->id,
        amqp_cstring_bytes(queue_name),
        passive, // Use parsed passive
        durable,
        exclusive,
        auto_delete,
        amqp_empty_table
    );
    check_amqp_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Queue Declare");

    // Return Info
    AngaraObject name_obj = angara_create_string_with_len((char*)r->queue.bytes, r->queue.len);

    AngaraObject k_q = angara_string_from_c("queue");
    AngaraObject kvs[] = { k_q, name_obj }; // Minimal return for now

    AngaraObject rec = angara_record_new_with_fields(1, kvs);
    angara_decref(k_q); angara_decref(name_obj);
    (void)queue;

    return rec;
}

// bind_queue(queue, exchange, routing_key)
AngaraObject Angara_Channel_bind_queue(int arg_count, AngaraObject* args) {
    dbg_amqp("bind_queue", "Called");
    ChannelData* ch = get_channel(args[0]);
    const char* q = AS_CSTRING(args[1]);
    const char* e = AS_CSTRING(args[2]);
    const char* k = AS_CSTRING(args[3]);

    amqp_queue_bind(ch->conn_data->conn, ch->id,
        amqp_cstring_bytes(q), amqp_cstring_bytes(e), amqp_cstring_bytes(k),
        amqp_empty_table);

    check_amqp_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Queue Bind");
    return angara_create_nil();
}

// exchange_declare(name, type, durable, auto_delete)
AngaraObject Angara_Channel_exchange_declare(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    const char* name = AS_CSTRING(args[1]);
    const char* type = AS_CSTRING(args[2]);

    // FIX: Parse 3 booleans: passive, durable, auto_delete
    int passive = (arg_count > 3 && angara_is_truthy(args[3])) ? 1 : 0;
    int durable = (arg_count > 4 && angara_is_truthy(args[4])) ? 1 : 0;
    int auto_delete = (arg_count > 5 && angara_is_truthy(args[5])) ? 1 : 0;

    amqp_exchange_declare(ch->conn_data->conn, ch->id,
        amqp_cstring_bytes(name), amqp_cstring_bytes(type),
        passive, // Use the parsed passive flag
        durable,
        auto_delete,
        0,
        amqp_empty_table);

    check_amqp_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Exchange Declare");
    return angara_create_nil();
}

// publish(exchange, key, body, [props])
AngaraObject Angara_Channel_publish(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    const char* ex = AS_CSTRING(args[1]);
    const char* key = AS_CSTRING(args[2]);
    const char* body = AS_CSTRING(args[3]);

    amqp_basic_properties_t props;
    memset(&props, 0, sizeof(props));

    // Handle properties record if present
    if (arg_count > 4 && IS_RECORD(args[4])) {
        AngaraObject p = args[4];

        // Content-Type
        AngaraObject ctype = angara_record_get(p, "contentType");
        if (!IS_NIL(ctype)) {
            props._flags |= AMQP_BASIC_CONTENT_TYPE_FLAG;
            props.content_type = amqp_cstring_bytes(AS_CSTRING(ctype));
        }

        // Correlation ID
        AngaraObject cid = angara_record_get(p, "correlationId");
        if (IS_NIL(cid)) cid = angara_record_get(p, "correlation_id");
        if (!IS_NIL(cid)) {
            props._flags |= AMQP_BASIC_CORRELATION_ID_FLAG;
            props.correlation_id = amqp_cstring_bytes(AS_CSTRING(cid));
        }

        // Reply To
        AngaraObject rep = angara_record_get(p, "replyTo");
        if (IS_NIL(rep)) rep = angara_record_get(p, "reply_to");
        if (!IS_NIL(rep)) {
            props._flags |= AMQP_BASIC_REPLY_TO_FLAG;
            props.reply_to = amqp_cstring_bytes(AS_CSTRING(rep));
        }
    }

    int res = amqp_basic_publish(ch->conn_data->conn, ch->id,
        amqp_cstring_bytes(ex), amqp_cstring_bytes(key),
        0, 0, &props, amqp_cstring_bytes(body));

    if (res < 0) angara_throw_error(amqp_error_string2(res));
    return angara_create_nil();
}

// subscribe(queue)
AngaraObject Angara_Channel_subscribe(int arg_count, AngaraObject* args) {
    dbg_amqp("subscribe", "Called");
    ChannelData* ch = get_channel(args[0]);
    const char* q = AS_CSTRING(args[1]);

    amqp_basic_consume(ch->conn_data->conn, ch->id,
        amqp_cstring_bytes(q), amqp_empty_bytes,
        0, 0, 0, amqp_empty_table);

    check_amqp_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Basic Consume");
    return angara_create_nil();
}

// prefetch(count)
AngaraObject Angara_Channel_prefetch(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    int count = (int)AS_I64(args[1]);
    amqp_basic_qos(ch->conn_data->conn, ch->id, 0, count, 0);
    check_amqp_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Basic QOS");
    return angara_create_nil();
}

// ack(tag)
AngaraObject Angara_Channel_ack(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    uint64_t tag = (uint64_t)AS_I64(args[1]);
    amqp_basic_ack(ch->conn_data->conn, ch->id, tag, 0);
    return angara_create_nil();
}

// nack(tag, requeue)
AngaraObject Angara_Channel_nack(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    uint64_t tag = (uint64_t)AS_I64(args[1]);
    int requeue = angara_is_truthy(args[2]);
    amqp_basic_nack(ch->conn_data->conn, ch->id, tag, 0, requeue);
    return angara_create_nil();
}

// close()
AngaraObject Angara_Channel_close(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_channel_close(ch->conn_data->conn, ch->id, AMQP_REPLY_SUCCESS);
    return angara_create_nil();
}

// next_message(timeout)
AngaraObject Angara_Channel_next_message(int arg_count, AngaraObject* args) {
    // dbg_amqp("next_message", "Called");
    ChannelData* ch_data = get_channel(args[0]);

    struct timeval timeout;
    struct timeval* timeout_ptr = NULL;

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

    amqp_maybe_release_buffers(ch_data->conn_data->conn);

    res = amqp_consume_message(ch_data->conn_data->conn, &envelope, timeout_ptr, 0);

    if (AMQP_RESPONSE_NORMAL != res.reply_type) {
        if (AMQP_RESPONSE_LIBRARY_EXCEPTION == res.reply_type &&
            AMQP_STATUS_TIMEOUT == res.library_error) {
            return angara_create_nil();
        }
        return angara_create_nil();
    }

    // Build Record
    AngaraObject body_obj = angara_create_string_with_len((char*)envelope.message.body.bytes, envelope.message.body.len);
    AngaraObject dtag_obj = angara_create_i64((int64_t)envelope.delivery_tag);

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

    AngaraObject k_body = angara_string_from_c("body");
    AngaraObject k_dtag = angara_string_from_c("delivery_tag");
    AngaraObject k_cid  = angara_string_from_c("correlationId");
    AngaraObject k_rep  = angara_string_from_c("replyTo");

    AngaraObject kvs[] = { k_body, body_obj, k_dtag, dtag_obj, k_cid, cid_obj, k_rep, reply_obj };
    AngaraObject rec = angara_record_new_with_fields(4, kvs);

    angara_decref(k_body); angara_decref(body_obj);
    angara_decref(k_dtag); angara_decref(dtag_obj);
    angara_decref(k_cid);  angara_decref(cid_obj);
    angara_decref(k_rep);  angara_decref(reply_obj);

    amqp_destroy_envelope(&envelope);
    return rec;
}

// --- ABI Definitions ---

static const AngaraMethodDef CHANNEL_METHODS[] = {
        {"queue_declare", (AngaraMethodFn)Angara_Channel_queue_declare, "sbbbb?->{}"},
        {"exchange_declare", (AngaraMethodFn)Angara_Channel_exchange_declare, "ssbbb?->n"},
        {"publish",       (AngaraMethodFn)Angara_Channel_publish,       "sss{}?->n"},
        {"subscribe",     (AngaraMethodFn)Angara_Channel_subscribe,     "s->n"},
        {"next_message",  (AngaraMethodFn)Angara_Channel_next_message,  "i?->{}?"},
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
        { "_channel", NULL, "->Channel", &CHANNEL_CLASS_DEF }, // Reg only
        { NULL, NULL, NULL, NULL }
};

ANGARA_MODULE_INIT(amqp) {
        *def_count = (sizeof(AMQP_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
        return AMQP_EXPORTS;
}
#include <rabbitmq-c/tcp_socket.h>
#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/framing.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

typedef struct { amqp_connection_state_t conn; amqp_socket_t* socket; int channel_count; bool is_connected; } ConnectionData;
typedef struct { int id; AngaraObject connection_obj; ConnectionData* conn_data; } ChannelData;

static void finalize_connection(void* data) {
    ConnectionData* c = (ConnectionData*)data;
    if (c->is_connected) amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(c->conn); free(c);
}
static void finalize_channel(void* data) {
    ChannelData* ch = (ChannelData*)data;
    if (ch->conn_data->is_connected) amqp_channel_close(ch->conn_data->conn, ch->id, AMQP_REPLY_SUCCESS);
    ang_api->decref(ch->connection_obj); free(ch);
}

static void check_reply(amqp_rpc_reply_t x, const char* ctx) {
    if (x.reply_type == AMQP_RESPONSE_NORMAL) return;
    char buf[512];
    if (x.reply_type == AMQP_RESPONSE_NONE) snprintf(buf, 512, "%s: missing RPC reply", ctx);
    else if (x.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) snprintf(buf, 512, "%s: %s", ctx, amqp_error_string2(x.library_error));
    else if (x.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION) {
        if (x.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
            amqp_connection_close_t* m = (amqp_connection_close_t*)x.reply.decoded;
            snprintf(buf, 512, "%s: Server Error %d: %.*s", ctx, m->reply_code, (int)m->reply_text.len, (char*)m->reply_text.bytes);
        } else snprintf(buf, 512, "%s: Unknown Server Exception", ctx);
    } else snprintf(buf, 512, "%s: Unknown error", ctx);
    ang_api->throw_error(buf);
}

AngaraObject Angara_amqp_connect(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("connect(url) expects a string."); return ang_nil(); }
    char* url_copy = strdup(ang_api->as_cstr(args[0]));
    struct amqp_connection_info ci;
    if (amqp_parse_url(url_copy, &ci)) { free(url_copy); ang_api->throw_error("Malformed AMQP URL."); return ang_nil(); }
    if (!ci.vhost || *ci.vhost == '\0') ci.vhost = "/";

    ConnectionData* conn = (ConnectionData*)malloc(sizeof(ConnectionData));
    conn->conn = amqp_new_connection(); conn->socket = amqp_tcp_socket_new(conn->conn);
    conn->channel_count = 0; conn->is_connected = false;
    if (!conn->socket) { free(url_copy); free(conn); ang_api->throw_error("TCP socket creation failed."); return ang_nil(); }
    if (amqp_socket_open(conn->socket, ci.host, ci.port)) {
        free(url_copy); amqp_destroy_connection(conn->conn); free(conn); ang_api->throw_error("TCP connect failed."); return ang_nil();
    }
    check_reply(amqp_login(conn->conn, ci.vhost, 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, ci.user, ci.password), "Login");
    conn->is_connected = true; free(url_copy);
    return ang_api->native_instance_new(conn, finalize_connection, "Connection");
}

AngaraObject Angara_Connection_channel(int arg_count, AngaraObject* args) {
    ConnectionData* conn = (ConnectionData*)ang_api->native_instance_data(args[0]);
    ChannelData* ch = (ChannelData*)malloc(sizeof(ChannelData));
    ch->conn_data = conn; ch->connection_obj = args[0];
    conn->channel_count++; ch->id = conn->channel_count;
    amqp_channel_open(conn->conn, ch->id);
    check_reply(amqp_get_rpc_reply(conn->conn), "Channel open");
    ang_api->incref(args[0]);
    return ang_api->native_instance_new(ch, finalize_channel, "Channel");
}

AngaraObject Angara_Connection_close(int arg_count, AngaraObject* args) {
    ConnectionData* c = (ConnectionData*)ang_api->native_instance_data(args[0]);
    if (c->is_connected) { amqp_connection_close(c->conn, AMQP_REPLY_SUCCESS); c->is_connected = false; }
    return ang_nil();
}

static ChannelData* get_channel(AngaraObject obj) { return (ChannelData*)ang_api->native_instance_data(obj); }

AngaraObject Angara_Channel_queue_declare(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]); const char* qname = ang_api->as_cstr(args[1]);
    int passive = (arg_count > 2 && ang_api->truthy(args[2])) ? 1 : 0;
    int durable = (arg_count > 3 && ang_api->truthy(args[3])) ? 1 : 0;
    int exclusive = (arg_count > 4 && ang_api->truthy(args[4])) ? 1 : 0;
    int auto_delete = (arg_count > 5 && ang_api->truthy(args[5])) ? 1 : 0;
    amqp_queue_declare_ok_t* r = amqp_queue_declare(ch->conn_data->conn, ch->id, amqp_cstring_bytes(qname), passive, durable, exclusive, auto_delete, amqp_empty_table);
    check_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Queue Declare");
    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "queue", ang_api->string_len((char*)r->queue.bytes, r->queue.len));
    return rec;
}

AngaraObject Angara_Channel_bind_queue(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_queue_bind(ch->conn_data->conn, ch->id, amqp_cstring_bytes(ang_api->as_cstr(args[1])), amqp_cstring_bytes(ang_api->as_cstr(args[2])), amqp_cstring_bytes(ang_api->as_cstr(args[3])), amqp_empty_table);
    check_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Queue Bind"); return ang_nil();
}

AngaraObject Angara_Channel_exchange_declare(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    int passive = (arg_count > 3 && ang_api->truthy(args[3])) ? 1 : 0;
    int durable = (arg_count > 4 && ang_api->truthy(args[4])) ? 1 : 0;
    int auto_delete = (arg_count > 5 && ang_api->truthy(args[5])) ? 1 : 0;
    amqp_exchange_declare(ch->conn_data->conn, ch->id, amqp_cstring_bytes(ang_api->as_cstr(args[1])), amqp_cstring_bytes(ang_api->as_cstr(args[2])), passive, durable, auto_delete, 0, amqp_empty_table);
    check_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Exchange Declare"); return ang_nil();
}

AngaraObject Angara_Channel_publish(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_basic_properties_t props; memset(&props, 0, sizeof(props));
    if (arg_count > 4 && IS_REC(args[4])) {
        AngaraObject ct = ang_api->record_get(args[4], "contentType");
        if (!ang_is_nil(ct)) { props._flags |= AMQP_BASIC_CONTENT_TYPE_FLAG; props.content_type = amqp_cstring_bytes(ang_api->as_cstr(ct)); }
        ang_api->decref(ct);
        AngaraObject cid = ang_api->record_get(args[4], "correlationId");
        if (ang_is_nil(cid)) { ang_api->decref(cid); cid = ang_api->record_get(args[4], "correlation_id"); }
        if (!ang_is_nil(cid)) { props._flags |= AMQP_BASIC_CORRELATION_ID_FLAG; props.correlation_id = amqp_cstring_bytes(ang_api->as_cstr(cid)); }
        ang_api->decref(cid);
    }
    int res = amqp_basic_publish(ch->conn_data->conn, ch->id, amqp_cstring_bytes(ang_api->as_cstr(args[1])), amqp_cstring_bytes(ang_api->as_cstr(args[2])), 0, 0, &props, amqp_cstring_bytes(ang_api->as_cstr(args[3])));
    if (res < 0) ang_api->throw_error(amqp_error_string2(res));
    return ang_nil();
}

AngaraObject Angara_Channel_subscribe(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_basic_consume(ch->conn_data->conn, ch->id, amqp_cstring_bytes(ang_api->as_cstr(args[1])), amqp_empty_bytes, 0, 0, 0, amqp_empty_table);
    check_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Basic Consume"); return ang_nil();
}

AngaraObject Angara_Channel_prefetch(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_basic_qos(ch->conn_data->conn, ch->id, 0, (uint32_t)ang_as_i64(args[1]), 0);
    check_reply(amqp_get_rpc_reply(ch->conn_data->conn), "Basic QOS"); return ang_nil();
}

AngaraObject Angara_Channel_ack(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_basic_ack(ch->conn_data->conn, ch->id, (uint64_t)ang_as_i64(args[1]), 0); return ang_nil();
}

AngaraObject Angara_Channel_nack(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_basic_nack(ch->conn_data->conn, ch->id, (uint64_t)ang_as_i64(args[1]), 0, ang_api->truthy(args[2])); return ang_nil();
}

AngaraObject Angara_Channel_close(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    amqp_channel_close(ch->conn_data->conn, ch->id, AMQP_REPLY_SUCCESS); return ang_nil();
}

AngaraObject Angara_Channel_next_message(int arg_count, AngaraObject* args) {
    ChannelData* ch = get_channel(args[0]);
    struct timeval timeout, *tp = NULL;
    if (arg_count == 2 && ang_is_i64(args[1])) { int64_t ms = ang_as_i64(args[1]); if(ms>=0){timeout.tv_sec=ms/1000;timeout.tv_usec=(ms%1000)*1000;tp=&timeout;} }
    amqp_envelope_t envelope; amqp_maybe_release_buffers(ch->conn_data->conn);
    amqp_rpc_reply_t res = amqp_consume_message(ch->conn_data->conn, &envelope, tp, 0);
    if (res.reply_type != AMQP_RESPONSE_NORMAL) return ang_nil();

    AngaraObject body = ang_api->string_len((char*)envelope.message.body.bytes, envelope.message.body.len);
    AngaraObject dtag = ang_i64((int64_t)envelope.delivery_tag);
    AngaraObject cid = ang_nil(), rep = ang_nil();
    if (envelope.message.properties._flags & AMQP_BASIC_CORRELATION_ID_FLAG)
        cid = ang_api->string_len((char*)envelope.message.properties.correlation_id.bytes, envelope.message.properties.correlation_id.len);
    if (envelope.message.properties._flags & AMQP_BASIC_REPLY_TO_FLAG)
        rep = ang_api->string_len((char*)envelope.message.properties.reply_to.bytes, envelope.message.properties.reply_to.len);

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "body", body); ang_api->record_set(rec, "delivery_tag", dtag);
    ang_api->record_set(rec, "correlationId", cid); ang_api->record_set(rec, "replyTo", rep);
    ang_api->decref(body); ang_api->decref(cid); ang_api->decref(rep);
    amqp_destroy_envelope(&envelope); return rec;
}

static const AngaraMethodDef CHANNEL_METHODS[] = {
    {"queue_declare",    (AngaraMethodFn)Angara_Channel_queue_declare,    "sbbbb?->{}"},
    {"exchange_declare", (AngaraMethodFn)Angara_Channel_exchange_declare, "ssbbb?->n"},
    {"publish",          (AngaraMethodFn)Angara_Channel_publish,          "sss{}?->n"},
    {"subscribe",        (AngaraMethodFn)Angara_Channel_subscribe,        "s->n"},
    {"next_message",     (AngaraMethodFn)Angara_Channel_next_message,     "i?->{}?"},
    {"ack",              (AngaraMethodFn)Angara_Channel_ack,              "i->n"},
    {"close",            (AngaraMethodFn)Angara_Channel_close,            "->n"},
    {"bind_queue",       (AngaraMethodFn)Angara_Channel_bind_queue,       "sss->n"},
    {"prefetch",         (AngaraMethodFn)Angara_Channel_prefetch,         "i->n"},
    {"nack",             (AngaraMethodFn)Angara_Channel_nack,             "ib->n"},
    {NULL, NULL, NULL}
};
static const AngaraMethodDef CONNECTION_METHODS[] = {
    {"channel", (AngaraMethodFn)Angara_Connection_channel, "->Channel"},
    {"close",   (AngaraMethodFn)Angara_Connection_close,   "->n"},
    {NULL, NULL, NULL}
};
static const AngaraClassDef CHANNEL_CLASS_DEF = { "Channel", NULL, CHANNEL_METHODS };
static const AngaraClassDef CONNECTION_CLASS_DEF = { "Connection", NULL, CONNECTION_METHODS };

static const AngaraFuncDef AMQP_EXPORTS[] = {
    {"connect",  Angara_amqp_connect, "s->Connection", &CONNECTION_CLASS_DEF},
    {"_channel", NULL, "->Channel", &CHANNEL_CLASS_DEF},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(amqp) {
    ang_api = api;
    *def_count = (sizeof(AMQP_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return AMQP_EXPORTS;
}
//
// mqtt.c — Angara MQTT module (16-byte ABI + vtable)
//
// Provides MQTT v3.1.1/v5.0 client functionality via Eclipse Mosquitto.
//
// Usage:
//   attach connect, Connection from mqtt;
//
//   let conn = connect("mqtt://localhost:1883");
//   conn.subscribe("sensors/temperature");
//   while (true) {
//     let msg = conn.next_message(1000);
//     if (msg != nil) {
//       io.println(1, msg["topic"] + ": " + msg["payload"]);
//     }
//   }
//

#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

// =============================================================================
// Concurrent Message Queue (same pattern as websocket.c)
// =============================================================================

typedef struct QueueNode { void* data; struct QueueNode* next; } QueueNode;
typedef struct {
    QueueNode* head;
    QueueNode* tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ConcurrentQueue;

static void queue_init(ConcurrentQueue* q) {
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void queue_push(ConcurrentQueue* q, void* data) {
    QueueNode* node = (QueueNode*)malloc(sizeof(QueueNode));
    node->data = data;
    node->next = NULL;
    pthread_mutex_lock(&q->mutex);
    if (q->tail) { q->tail->next = node; q->tail = node; }
    else { q->head = q->tail = node; }
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

static void* queue_pop(ConcurrentQueue* q, int timeout_ms) {
    pthread_mutex_lock(&q->mutex);
    if (!q->head && timeout_ms != 0) {
        if (timeout_ms < 0) {
            // Block indefinitely
            while (!q->head) pthread_cond_wait(&q->cond, &q->mutex);
        } else {
            // Timed wait
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            while (!q->head) {
                if (pthread_cond_timedwait(&q->cond, &q->mutex, &ts) != 0) break;
            }
        }
    }
    if (!q->head) { pthread_mutex_unlock(&q->mutex); return NULL; }
    QueueNode* node = q->head;
    void* data = node->data;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    pthread_mutex_unlock(&q->mutex);
    free(node);
    return data;
}

static void queue_free_all(ConcurrentQueue* q, void (*free_fn)(void*)) {
    pthread_mutex_lock(&q->mutex);
    QueueNode* cur = q->head;
    while (cur) {
        QueueNode* next = cur->next;
        if (free_fn) free_fn(cur->data);
        free(cur);
        cur = next;
    }
    q->head = q->tail = NULL;
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

// =============================================================================
// MQTT Message Structure (stored in queue)
// =============================================================================

typedef struct {
    char* topic;
    char* payload;
    int qos;
    int mid;
} MqttMessage;

static MqttMessage* mqtt_msg_new(const char* topic, const void* payload, int payload_len, int qos, int mid) {
    MqttMessage* msg = (MqttMessage*)malloc(sizeof(MqttMessage));
    msg->topic = strdup(topic);
    msg->payload = (char*)malloc(payload_len + 1);
    memcpy(msg->payload, payload, payload_len);
    msg->payload[payload_len] = '\0';
    msg->qos = qos;
    msg->mid = mid;
    return msg;
}

static void mqtt_msg_free(void* ptr) {
    MqttMessage* msg = (MqttMessage*)ptr;
    if (!msg) return;
    free(msg->topic);
    free(msg->payload);
    free(msg);
}

// =============================================================================
// Connection Data (native instance)
// =============================================================================

typedef struct {
    struct mosquitto* mosq;
    bool is_connected;
    ConcurrentQueue incoming;
    pthread_mutex_t state_mutex;
    char client_id[64];
} MqttConnectionData;

static void finalize_connection(void* data) {
    MqttConnectionData* c = (MqttConnectionData*)data;
    if (c->mosq) {
        mosquitto_disconnect(c->mosq);
        mosquitto_loop_stop(c->mosq, false);
        mosquitto_destroy(c->mosq);
    }
    queue_free_all(&c->incoming, mqtt_msg_free);
    pthread_mutex_destroy(&c->state_mutex);
    free(c);
}

// =============================================================================
// Mosquitto Callbacks
// =============================================================================

static void on_connect(struct mosquitto* mosq, void* userdata, int rc) {
    MqttConnectionData* conn = (MqttConnectionData*)userdata;
    pthread_mutex_lock(&conn->state_mutex);
    conn->is_connected = (rc == 0);
    pthread_mutex_unlock(&conn->state_mutex);
}

static void on_disconnect(struct mosquitto* mosq, void* userdata, int rc) {
    MqttConnectionData* conn = (MqttConnectionData*)userdata;
    pthread_mutex_lock(&conn->state_mutex);
    conn->is_connected = false;
    pthread_mutex_unlock(&conn->state_mutex);
}

static void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg) {
    MqttConnectionData* conn = (MqttConnectionData*)userdata;
    MqttMessage* m = mqtt_msg_new(msg->topic, msg->payload, msg->payloadlen, msg->qos, msg->mid);
    queue_push(&conn->incoming, m);
}

// =============================================================================
// URL Parsing Helper
// =============================================================================

typedef struct {
    char host[256];
    int port;
    char username[128];
    char password[128];
    bool has_auth;
} MqttUrlInfo;

static int parse_mqtt_url(const char* url, MqttUrlInfo* info) {
    memset(info, 0, sizeof(MqttUrlInfo));
    info->port = 1883; // default MQTT port

    // Skip scheme
    const char* p = url;
    if (strncmp(p, "mqtt://", 7) == 0) p += 7;
    else if (strncmp(p, "mqtts://", 8) == 0) { p += 8; info->port = 8883; }
    else if (strncmp(p, "tcp://", 6) == 0) p += 6;

    // Extract userinfo if present
    const char* at = strchr(p, '@');
    if (at) {
        const char* colon = strchr(p, ':');
        if (colon && colon < at) {
            size_t ulen = colon - p;
            if (ulen >= sizeof(info->username)) ulen = sizeof(info->username) - 1;
            memcpy(info->username, p, ulen);
            info->username[ulen] = '\0';
            size_t plen = at - colon - 1;
            if (plen >= sizeof(info->password)) plen = sizeof(info->password) - 1;
            memcpy(info->password, colon + 1, plen);
            info->password[plen] = '\0';
            info->has_auth = true;
        }
        p = at + 1;
    }

    // Extract host and port
    const char* port_str = strchr(p, ':');
    const char* path = strchr(p, '/');
    const char* end = path ? path : (p + strlen(p));

    if (port_str && port_str < end) {
        size_t hlen = port_str - p;
        if (hlen >= sizeof(info->host)) hlen = sizeof(info->host) - 1;
        memcpy(info->host, p, hlen);
        info->host[hlen] = '\0';
        info->port = atoi(port_str + 1);
    } else {
        size_t hlen = end - p;
        if (hlen >= sizeof(info->host)) hlen = sizeof(info->host) - 1;
        memcpy(info->host, p, hlen);
        info->host[hlen] = '\0';
    }

    return (info->host[0] != '\0') ? 0 : -1;
}

// =============================================================================
// Exported Functions
// =============================================================================

// mqtt.connect(url: string, options?: record) -> Connection
// Options record: {clientId: string, keepalive: i64, cleanSession: bool}
AngaraObject Angara_mqtt_connect(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("mqtt.connect(url, options?) expects a string URL.");
        return ang_nil();
    }

    static bool mosquitto_initialized = false;
    if (!mosquitto_initialized) {
        mosquitto_lib_init();
        mosquitto_initialized = true;
    }

    const char* url = ang_api->as_cstr(args[0]);
    MqttUrlInfo info;
    if (parse_mqtt_url(url, &info) != 0) {
        ang_api->throw_error("mqtt.connect: malformed URL. Expected mqtt://[user:pass@]host[:port]");
        return ang_nil();
    }

    // Parse options
    const char* client_id = NULL;
    char client_id_buf[64];
    int keepalive = 60;
    bool clean_session = true;

    if (arg_count >= 2 && IS_REC(args[1])) {
        AngaraObject cid = ang_api->record_get(args[1], "clientId");
        if (!ang_is_nil(cid) && IS_STR(cid)) {
            client_id = ang_api->as_cstr(cid);
        }
        ang_api->decref(cid);

        AngaraObject ka = ang_api->record_get(args[1], "keepalive");
        if (!ang_is_nil(ka) && ang_is_i64(ka)) {
            keepalive = (int)ang_as_i64(ka);
            if (keepalive < 0) keepalive = 60;
        }
        ang_api->decref(ka);

        AngaraObject cs = ang_api->record_get(args[1], "cleanSession");
        if (!ang_is_nil(cs) && ang_is_bool(cs)) {
            clean_session = ang_as_bool(cs);
        }
        ang_api->decref(cs);
    }

    // Generate client ID if not provided
    if (!client_id) {
        snprintf(client_id_buf, sizeof(client_id_buf), "angara_%lx_%d", (unsigned long)time(NULL), rand() % 10000);
        client_id = client_id_buf;
    }

    // Create connection data
    MqttConnectionData* conn = (MqttConnectionData*)calloc(1, sizeof(MqttConnectionData));
    queue_init(&conn->incoming);
    pthread_mutex_init(&conn->state_mutex, NULL);
    snprintf(conn->client_id, sizeof(conn->client_id), "%s", client_id);

    // Create mosquitto instance
    conn->mosq = mosquitto_new(client_id, clean_session, conn);
    if (!conn->mosq) {
        pthread_mutex_destroy(&conn->state_mutex);
        queue_free_all(&conn->incoming, mqtt_msg_free);
        free(conn);
        ang_api->throw_error("mqtt.connect: failed to create mosquitto instance.");
        return ang_nil();
    }

    // Set credentials if provided
    if (info.has_auth) {
        if (mosquitto_username_pw_set(conn->mosq, info.username, info.password) != MOSQ_ERR_SUCCESS) {
            mosquitto_destroy(conn->mosq);
            pthread_mutex_destroy(&conn->state_mutex);
            queue_free_all(&conn->incoming, mqtt_msg_free);
            free(conn);
            ang_api->throw_error("mqtt.connect: failed to set credentials.");
            return ang_nil();
        }
    }

    // Set callbacks
    mosquitto_connect_callback_set(conn->mosq, on_connect);
    mosquitto_disconnect_callback_set(conn->mosq, on_disconnect);
    mosquitto_message_callback_set(conn->mosq, on_message);

    // Mark as connected immediately (synchronous connect will block until connected)
    conn->is_connected = true;

    // Connect synchronously, then start background loop for callbacks.
    // Using synchronous connect avoids MOSQ_ERR_NO_CONN issues with connect_async
    // when reconnecting after a prior disconnect in the same process.
    int rc = mosquitto_connect(conn->mosq, info.host, info.port, keepalive);
    if (rc != MOSQ_ERR_SUCCESS) {
        char buf[256];
        snprintf(buf, sizeof(buf), "mqtt.connect: failed to connect to %s:%d — %s",
                 info.host, info.port, mosquitto_strerror(rc));
        mosquitto_destroy(conn->mosq);
        pthread_mutex_destroy(&conn->state_mutex);
        queue_free_all(&conn->incoming, mqtt_msg_free);
        free(conn);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    rc = mosquitto_loop_start(conn->mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        char buf[256];
        snprintf(buf, sizeof(buf), "mqtt.connect: failed to start network loop — %s", mosquitto_strerror(rc));
        mosquitto_disconnect(conn->mosq);
        mosquitto_destroy(conn->mosq);
        pthread_mutex_destroy(&conn->state_mutex);
        queue_free_all(&conn->incoming, mqtt_msg_free);
        free(conn);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    return ang_api->native_instance_new(conn, finalize_connection, "Connection");
}

// =============================================================================
// Connection Methods
// =============================================================================

// Connection.publish(topic: string, payload: string, qos?: i64) -> nil
AngaraObject Angara_Connection_publish(int arg_count, AngaraObject* args) {
    MqttConnectionData* conn = (MqttConnectionData*)ang_api->native_instance_data(args[0]);
    if (!conn || !conn->mosq) { ang_api->throw_error("mqtt: invalid connection."); return ang_nil(); }

    if (arg_count < 3 || !IS_STR(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("Connection.publish(topic, payload, qos?) expects strings.");
        return ang_nil();
    }

    const char* topic = ang_api->as_cstr(args[1]);
    const char* payload = ang_api->as_cstr(args[2]);
    size_t payload_len = ang_api->str_len(args[2]);
    int qos = 0;
    if (arg_count >= 4 && ang_is_i64(args[3])) qos = (int)ang_as_i64(args[3]);
    if (qos < 0) qos = 0;
    if (qos > 2) qos = 2;

    int rc = mosquitto_publish(conn->mosq, NULL, topic, (int)payload_len, payload, qos, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        char buf[256];
        snprintf(buf, sizeof(buf), "mqtt.publish failed: %s", mosquitto_strerror(rc));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

// Connection.subscribe(topic: string, qos?: i64) -> nil
AngaraObject Angara_Connection_subscribe(int arg_count, AngaraObject* args) {
    MqttConnectionData* conn = (MqttConnectionData*)ang_api->native_instance_data(args[0]);
    if (!conn || !conn->mosq) { ang_api->throw_error("mqtt: invalid connection."); return ang_nil(); }

    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("Connection.subscribe(topic, qos?) expects a string topic.");
        return ang_nil();
    }

    const char* topic = ang_api->as_cstr(args[1]);
    int qos = 0;
    if (arg_count >= 3 && ang_is_i64(args[2])) qos = (int)ang_as_i64(args[2]);
    if (qos < 0) qos = 0;
    if (qos > 2) qos = 2;

    int rc = mosquitto_subscribe(conn->mosq, NULL, topic, qos);
    if (rc != MOSQ_ERR_SUCCESS) {
        char buf[256];
        snprintf(buf, sizeof(buf), "mqtt.subscribe failed: %s", mosquitto_strerror(rc));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

// Connection.unsubscribe(topic: string) -> nil
AngaraObject Angara_Connection_unsubscribe(int arg_count, AngaraObject* args) {
    MqttConnectionData* conn = (MqttConnectionData*)ang_api->native_instance_data(args[0]);
    if (!conn || !conn->mosq) { ang_api->throw_error("mqtt: invalid connection."); return ang_nil(); }

    if (arg_count < 2 || !IS_STR(args[1])) {
        ang_api->throw_error("Connection.unsubscribe(topic) expects a string topic.");
        return ang_nil();
    }

    int rc = mosquitto_unsubscribe(conn->mosq, NULL, ang_api->as_cstr(args[1]));
    if (rc != MOSQ_ERR_SUCCESS) {
        char buf[256];
        snprintf(buf, sizeof(buf), "mqtt.unsubscribe failed: %s", mosquitto_strerror(rc));
        ang_api->throw_error(buf);
    }
    return ang_nil();
}

// Connection.next_message(timeout_ms?: i64) -> record?
// Returns {topic: string, payload: string, qos: i64} or nil
AngaraObject Angara_Connection_next_message(int arg_count, AngaraObject* args) {
    MqttConnectionData* conn = (MqttConnectionData*)ang_api->native_instance_data(args[0]);
    if (!conn || !conn->mosq) { ang_api->throw_error("mqtt: invalid connection."); return ang_nil(); }

    int timeout_ms = 0; // default: non-blocking
    if (arg_count >= 2 && ang_is_i64(args[1])) {
        timeout_ms = (int)ang_as_i64(args[1]);
    }

    MqttMessage* msg = (MqttMessage*)queue_pop(&conn->incoming, timeout_ms);
    if (!msg) return ang_nil();

    AngaraObject topic_str = ang_api->string(msg->topic);
    AngaraObject payload_str = ang_api->string(msg->payload);
    AngaraObject qos_val = ang_i64(msg->qos);

    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "topic", topic_str);
    ang_api->record_set(rec, "payload", payload_str);
    ang_api->record_set(rec, "qos", qos_val);

    ang_api->decref(topic_str);
    ang_api->decref(payload_str);
    mqtt_msg_free(msg);
    return rec;
}

// Connection.is_connected() -> bool
AngaraObject Angara_Connection_is_connected(int arg_count, AngaraObject* args) {
    MqttConnectionData* conn = (MqttConnectionData*)ang_api->native_instance_data(args[0]);
    if (!conn) return ang_bool(false);
    pthread_mutex_lock(&conn->state_mutex);
    bool connected = conn->is_connected;
    pthread_mutex_unlock(&conn->state_mutex);
    return ang_bool(connected);
}

// Connection.disconnect() -> nil
AngaraObject Angara_Connection_disconnect(int arg_count, AngaraObject* args) {
    MqttConnectionData* conn = (MqttConnectionData*)ang_api->native_instance_data(args[0]);
    if (!conn || !conn->mosq) return ang_nil();
    mosquitto_disconnect(conn->mosq);
    mosquitto_loop_stop(conn->mosq, false);
    return ang_nil();
}

// Connection.client_id() -> string
AngaraObject Angara_Connection_client_id(int arg_count, AngaraObject* args) {
    MqttConnectionData* conn = (MqttConnectionData*)ang_api->native_instance_data(args[0]);
    if (!conn) return ang_api->string("");
    return ang_api->string(conn->client_id);
}

// =============================================================================
// Module Export Table
// =============================================================================

static const AngaraMethodDef CONNECTION_METHODS[] = {
    {"publish",      (AngaraMethodFn)Angara_Connection_publish,      "ssi?->n"},
    {"subscribe",    (AngaraMethodFn)Angara_Connection_subscribe,    "si?->n"},
    {"unsubscribe",  (AngaraMethodFn)Angara_Connection_unsubscribe,  "s->n"},
    {"next_message", (AngaraMethodFn)Angara_Connection_next_message, "i?->a"},
    {"is_connected", (AngaraMethodFn)Angara_Connection_is_connected, "->b"},
    {"disconnect",   (AngaraMethodFn)Angara_Connection_disconnect,   "->n"},
    {"client_id",    (AngaraMethodFn)Angara_Connection_client_id,    "->s"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef CONNECTION_CLASS_DEF = { "Connection", NULL, CONNECTION_METHODS };

static const AngaraFuncDef MQTT_EXPORTS[] = {
    {"connect", Angara_mqtt_connect, "s{}?->Connection", &CONNECTION_CLASS_DEF},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(mqtt) {
    ang_api = api;
    *def_count = (sizeof(MQTT_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return MQTT_EXPORTS;
}
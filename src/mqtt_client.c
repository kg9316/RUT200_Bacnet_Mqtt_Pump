#include "mqtt_client.h"
#include "logger.h"
#include "gk_cloud.h"
#include "tag_registry.h"

#include <mosquitto.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static struct mosquitto *g_mosq = NULL;
static bool g_mqtt_connected = false;
static uint64_t next_reconnect_ms;
static unsigned token_generation;
MQTT_SETTINGS g_mqtt_settings = { .ca_file = MQTT_CA_BUNDLE };
MQTT_DIAGNOSTICS g_mqtt_diagnostics;

static void mqtt_error(const char *stage, int rc)
{
    ++g_mqtt_diagnostics.failures;
    snprintf(g_mqtt_diagnostics.error, sizeof(g_mqtt_diagnostics.error), "%s: %s (rc=%d)", stage, mosquitto_strerror(rc), rc);
    LOG_WARNF("MQTT %s", g_mqtt_diagnostics.error);
}

static void mqtt_on_publish(struct mosquitto *mosq, void *userdata, int mid)
{
    (void)mosq; (void)userdata; (void)mid;
    ++g_mqtt_diagnostics.sent;
    g_mqtt_diagnostics.last_sent = unix_time_now();
}

char g_mqtt_host[128] = DEFAULT_MQTT_HOST;
int g_mqtt_port = DEFAULT_MQTT_PORT;
char g_topic_root[128] = "bacnet";
unsigned g_poll_ms = DEFAULT_POLL_MS;
unsigned g_discovery_ms = DEFAULT_DISCOVERY_MS;
unsigned g_max_age_sec = DEFAULT_MAX_AGE_SEC;
unsigned g_rp_timeout_ms = DEFAULT_RP_TIMEOUT_MS;
char g_bacnet_interface[64] = DEFAULT_BACNET_IF;
bool g_enabled = true;

static void mqtt_on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
    (void)mosq;
    (void)userdata;
    g_mqtt_connected = (rc == 0);
    if (rc == 0) {
        g_mqtt_diagnostics.error[0] = 0;
        size_t i, j;
        for (i = 0; i < MAX_DEVICES; ++i) {
            for (j = 0; j < g_devices[i].point_count; ++j) {
                g_devices[i].points[j].have_published_value = false;
                g_devices[i].points[j].config_published = false;
            }
        }
        LOG_INFOF("MQTT connected to %s:%d", g_mqtt_host, g_mqtt_port);
    } else {
        ++g_mqtt_diagnostics.failures;
        snprintf(g_mqtt_diagnostics.error, sizeof(g_mqtt_diagnostics.error), "Broker rejected connection: %s (CONNACK=%d)", mosquitto_connack_string(rc), rc);
        LOG_ERRORF("MQTT %s", g_mqtt_diagnostics.error);
        if (g_mqtt_settings.gk_cloud && (rc == 4 || rc == 5)) gk_cloud_invalidate();
    }
}

static void mqtt_on_disconnect(struct mosquitto *mosq, void *userdata, int rc)
{
    (void)mosq;
    (void)userdata;
    g_mqtt_connected = false;
    if (rc) mqtt_error("disconnected", rc);
}

static void mqtt_publish_raw(const char *topic, const char *payload, bool retain)
{
    int rc;

    if (!g_mosq || !g_mqtt_connected)
        return;

    rc = mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 0, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        mqtt_error("publish failed", rc);
        g_mqtt_connected = false;
    } else ++g_mqtt_diagnostics.queued;
}

static void mqtt_make_point_topic(char *dst, size_t n, const char *branch,
                                  const DEVICE_STATE *device,
                                  const POINT_STATE *point)
{
    char device_name[NAME_LEN];
    char point_name[NAME_LEN];
    char device_segment[NAME_LEN + 16];

    safe_copy(device_name, sizeof(device_name),
              device->have_name ? device->name : "unknown-device");
    safe_copy(point_name, sizeof(point_name),
              point->name[0] ? point->name : "unnamed");
    topic_sanitize(device_name);
    topic_sanitize(point_name);

    snprintf(device_segment, sizeof(device_segment), "%s_%lu",
             device_name, (unsigned long)device->device_id);

    snprintf(dst, n, "%s/%s/%s/%s",
             g_topic_root,
             branch,
             device_segment,
             point_name);
}

void mqtt_publish_config(DEVICE_STATE *device, POINT_STATE *point)
{
    char topic[TOPIC_LEN];
    char payload[PAYLOAD_LEN];

    if (g_mqtt_settings.gk_cloud) return;

    if (!point->metadata_complete)
        return;

    mqtt_make_point_topic(topic, sizeof(topic), "config", device, point);
    snprintf(payload, sizeof(payload),
             "{\"unit\":\"%s\",\"description\":\"%s\",\"objectType\":%u,\"objectInstance\":%lu}",
             point->unit,
             point->description,
             (unsigned)point->object_type,
             (unsigned long)point->object_instance);

    mqtt_publish_raw(topic, payload, true);
    if (g_mqtt_connected)
        point->config_published = true;
}

void mqtt_publish_live_if_needed(DEVICE_STATE *device, POINT_STATE *point)
{
    bool changed = false;
    time_t now = unix_time_now();
    bool expired;
    char topic[TOPIC_LEN];
    char payload[PAYLOAD_LEN];

    if (!point->have_value)
        return;

    expired = !point->have_published_value || !point->last_publish ||
              (unsigned)(now - point->last_publish) >= g_max_age_sec;

    switch (point->value_kind) {
        case VALUE_NUMBER:
        case VALUE_ENUM:
            changed = !point->have_published_value ||
                      double_changed(point->numeric_value, point->last_pub_numeric);
            break;
        case VALUE_BOOL:
            changed = !point->have_published_value ||
                      point->bool_value != point->last_pub_bool;
            break;
        case VALUE_STRING:
            changed = !point->have_published_value ||
                      strcmp(point->string_value, point->last_pub_string) != 0;
            break;
        default:
            return;
    }

    if (!changed && !expired)
        return;

    if (g_mqtt_settings.gk_cloud) {
        char *message = gk_cloud_message(device, point, topic, sizeof(topic));
        if (!message) return;
        mqtt_publish_raw(topic, message, false);
        free(message);
        goto published;
    }

    mqtt_make_point_topic(topic, sizeof(topic), "status", device, point);

    if (point->value_kind == VALUE_NUMBER || point->value_kind == VALUE_ENUM) {
        snprintf(payload, sizeof(payload),
                 "{\"value\":%.10g,\"timestamp\":%lld}",
                 point->numeric_value,
                 (long long)now);
    } else if (point->value_kind == VALUE_BOOL) {
        snprintf(payload, sizeof(payload),
                 "{\"value\":%s,\"timestamp\":%lld}",
                 point->bool_value ? "true" : "false",
                 (long long)now);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"value\":\"%s\",\"timestamp\":%lld}",
                 point->string_value,
                 (long long)now);
    }

    mqtt_publish_raw(topic, payload, false);
published:
    if (!g_mqtt_connected)
        return;

    point->have_published_value = true;
    point->last_publish = now;

    if (point->value_kind == VALUE_NUMBER || point->value_kind == VALUE_ENUM)
        point->last_pub_numeric = point->numeric_value;
    else if (point->value_kind == VALUE_BOOL)
        point->last_pub_bool = point->bool_value;
    else
        safe_copy(point->last_pub_string,
                  sizeof(point->last_pub_string),
                  point->string_value);
}

static void destroy_client(void)
{
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }
    g_mqtt_connected = false;
}

static int create_client(void)
{
    char client_id[160];
    int rc;
    const char *access = g_mqtt_settings.gk_cloud ? gk_cloud_token() : NULL;
    if (g_mqtt_settings.gk_cloud && !access) return MOSQ_ERR_AUTH;
    if (g_mqtt_port < 1 || g_mqtt_port > 65535) return MOSQ_ERR_INVAL;
    if (g_mqtt_settings.gk_cloud) {
        if (!g_mqtt_settings.controller_id[0] || strpbrk(g_mqtt_settings.controller_id, "/+#"))
            return MOSQ_ERR_INVAL;
        snprintf(client_id, sizeof(client_id), "c/%s/stream/Connector", g_mqtt_settings.controller_id);
    } else {
        safe_copy(client_id, sizeof(client_id), "gk-bacnet-mqtt");
    }
    g_mosq = mosquitto_new(client_id, true, NULL);
    if (!g_mosq) return MOSQ_ERR_NOMEM;
    mosquitto_connect_callback_set(g_mosq, mqtt_on_connect);
    mosquitto_disconnect_callback_set(g_mosq, mqtt_on_disconnect);
    mosquitto_publish_callback_set(g_mosq, mqtt_on_publish);
    if (g_mqtt_settings.tls || g_mqtt_settings.gk_cloud) {
        const char *cert = !g_mqtt_settings.gk_cloud && g_mqtt_settings.cert_file[0] ? g_mqtt_settings.cert_file : NULL;
        const char *key = !g_mqtt_settings.gk_cloud && g_mqtt_settings.key_file[0] ? g_mqtt_settings.key_file : NULL;
        if (!!cert != !!key) { rc = MOSQ_ERR_INVAL; goto failed; }
        rc = mosquitto_tls_set(g_mosq,
            g_mqtt_settings.ca_file[0] ? g_mqtt_settings.ca_file : MQTT_CA_BUNDLE,
            NULL, cert, key, NULL);
        if (rc != MOSQ_ERR_SUCCESS) goto failed;
        rc = mosquitto_tls_opts_set(g_mosq, 1, "tlsv1.2", NULL);
        if (rc != MOSQ_ERR_SUCCESS) goto failed;
        rc = mosquitto_tls_insecure_set(g_mosq, false);
        if (rc != MOSQ_ERR_SUCCESS) goto failed;
    }
    if (access) {
        rc = mosquitto_username_pw_set(g_mosq, g_mqtt_settings.controller_id, access);
        if (rc != MOSQ_ERR_SUCCESS) goto failed;
        token_generation = gk_cloud_token_generation();
    }
    rc = mosquitto_connect_async(g_mosq, g_mqtt_host, g_mqtt_port, 30);
    if (rc == MOSQ_ERR_SUCCESS) return rc;
failed:
    mqtt_error("connection setup failed", rc);
    destroy_client();
    return rc;
}

int mqtt_client_init(void)
{
    int rc = mosquitto_lib_init();
    if (rc != MOSQ_ERR_SUCCESS) return rc;
    if (gk_cloud_init() != 0) return MOSQ_ERR_UNKNOWN;
    if (g_mqtt_settings.gk_cloud) tag_registry_init();
    gk_cloud_reset();
    /* First connection is made by loop(), after GK authentication if enabled. */
    next_reconnect_ms = 0;
    return MOSQ_ERR_SUCCESS;
}

bool mqtt_client_is_connected(void) { return g_mqtt_connected; }

void mqtt_client_reconnect(void)
{
    destroy_client();
    gk_cloud_reset();
    if (g_mqtt_settings.gk_cloud) tag_registry_init();
    next_reconnect_ms = 0;
    g_mqtt_diagnostics.error[0] = 0;
}

void mqtt_client_loop(void)
{
    uint64_t now = monotonic_ms();
    int rc;
    gk_cloud_loop();
    if (g_mqtt_settings.gk_cloud) {
        if (!gk_cloud_token()) { destroy_client(); return; }
        if (g_mosq && token_generation != gk_cloud_token_generation()) {
            destroy_client();
            next_reconnect_ms = 0;
        }
    }
    if (!g_mosq) {
        if (now < next_reconnect_ms) return;
        next_reconnect_ms = now + 5000;
        if (create_client() != MOSQ_ERR_SUCCESS) return;
        next_reconnect_ms = now + 30000; /* allow TLS handshake and CONNACK */
    }
    rc = mosquitto_loop(g_mosq, 0, 1);
    if (rc != MOSQ_ERR_SUCCESS || (!g_mqtt_connected && now >= next_reconnect_ms)) {
        mqtt_error(rc == MOSQ_ERR_SUCCESS ? "CONNACK timeout" : "network loop failed", rc == MOSQ_ERR_SUCCESS ? MOSQ_ERR_CONN_LOST : rc);
        destroy_client();
        next_reconnect_ms = now + 5000;
    }
}

void mqtt_client_cleanup(void)
{
    destroy_client();
    tag_registry_cleanup();
    gk_cloud_cleanup();
    mosquitto_lib_cleanup();
}

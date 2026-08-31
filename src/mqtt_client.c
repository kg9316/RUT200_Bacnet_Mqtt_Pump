#include "mqtt_client.h"
#include "logger.h"

#include <mosquitto.h>
#include <stdio.h>
#include <string.h>

static struct mosquitto *g_mosq = NULL;
static bool g_mqtt_connected = false;

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
    if (rc == 0)
        LOG_INFOF("MQTT connected to %s:%d", g_mqtt_host, g_mqtt_port);
    else
        LOG_ERRORF("MQTT connect failed rc=%d", rc);
}

static void mqtt_on_disconnect(struct mosquitto *mosq, void *userdata, int rc)
{
    (void)mosq;
    (void)userdata;
    g_mqtt_connected = false;
    LOG_WARNF("MQTT disconnected rc=%d", rc);
}

static void mqtt_publish_raw(const char *topic, const char *payload, bool retain)
{
    int rc;

    if (!g_mosq || !g_mqtt_connected)
        return;

    rc = mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 0, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERRORF("MQTT publish failed topic=%s: %s", topic, mosquitto_strerror(rc));
        g_mqtt_connected = false;
    }
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

    mqtt_make_point_topic(topic, sizeof(topic), "status", device, point);

    if (point->value_kind == VALUE_NUMBER) {
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
    if (!g_mqtt_connected)
        return;

    point->have_published_value = true;
    point->last_publish = now;

    if (point->value_kind == VALUE_NUMBER)
        point->last_pub_numeric = point->numeric_value;
    else if (point->value_kind == VALUE_BOOL)
        point->last_pub_bool = point->bool_value;
    else
        safe_copy(point->last_pub_string,
                  sizeof(point->last_pub_string),
                  point->string_value);
}

int mqtt_client_init(void)
{
    int rc = mosquitto_lib_init();

    if (rc != MOSQ_ERR_SUCCESS)
        return rc;

    g_mosq = mosquitto_new("gk-bacnet-mqtt", true, NULL);
    if (!g_mosq)
        return MOSQ_ERR_NOMEM;

    mosquitto_connect_callback_set(g_mosq, mqtt_on_connect);
    mosquitto_disconnect_callback_set(g_mosq, mqtt_on_disconnect);
    mosquitto_reconnect_delay_set(g_mosq, 2, 30, true);

    return mosquitto_connect_async(g_mosq, g_mqtt_host, g_mqtt_port, 30);
}

bool mqtt_client_is_connected(void)
{
    return g_mqtt_connected;
}

void mqtt_client_reconnect(void)
{
    int rc;

    if (!g_mosq)
        return;

    mosquitto_disconnect(g_mosq);
    g_mqtt_connected = false;

    rc = mosquitto_connect_async(g_mosq, g_mqtt_host, g_mqtt_port, 30);
    if (rc != MOSQ_ERR_SUCCESS)
        LOG_WARNF("MQTT reconnect to %s:%d failed rc=%d", g_mqtt_host, g_mqtt_port, rc);
}

void mqtt_client_loop(void)
{
    int rc;
    static uint64_t next_reconnect_ms = 0;

    if (!g_mosq)
        return;

    rc = mosquitto_loop(g_mosq, 0, 1);
    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
        g_mqtt_connected = false;
        LOG_WARNF("MQTT loop error: %s; reconnecting", mosquitto_strerror(rc));
        mosquitto_reconnect_async(g_mosq);
    }

    /* mosquitto_loop() alone does not retry a connection that never
     * succeeded (e.g. broker unreachable at the initial connect_async())
     * - it just keeps returning MOSQ_ERR_NO_CONN forever. Retry on our
     * own timer whenever we're not connected. */
    if (!g_mqtt_connected) {
        uint64_t now = monotonic_ms();
        if (now >= next_reconnect_ms) {
            mosquitto_reconnect_async(g_mosq);
            next_reconnect_ms = now + 5000;
        }
    }
}

void mqtt_client_cleanup(void)
{
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }

    mosquitto_lib_cleanup();
}

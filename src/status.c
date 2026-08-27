#include "status.h"

#include "gateway.h"
#include "mqtt_client.h"

#include <stdio.h>
#include <unistd.h>

#define STATUS_FILE "/tmp/gk-bacnet-mqtt-status.json"

static uint64_t g_next_status_ms = 0;

static void count_runtime(size_t *devices, size_t *points)
{
    size_t i;
    size_t d = 0;
    size_t p = 0;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (!g_devices[i].used)
            continue;
        d++;
        p += g_devices[i].point_count;
    }

    *devices = d;
    *points = p;
}

void status_write_now(void)
{
    char tmp_path[128];
    FILE *f;
    size_t devices = 0;
    size_t points = 0;

    count_runtime(&devices, &points);
    snprintf(tmp_path, sizeof(tmp_path), "%s.%ld", STATUS_FILE, (long)getpid());

    f = fopen(tmp_path, "w");
    if (!f)
        return;

    fprintf(f,
            "{\"running\":true,\"mqttConnected\":%s,\"devices\":%lu,\"points\":%lu,"
            "\"mqttHost\":\"%s\",\"mqttPort\":%d,\"topicRoot\":\"%s\","
            "\"pollMs\":%u,\"discoveryMs\":%u,\"maxAgeSec\":%u,\"timestamp\":%lld}\n",
            mqtt_client_is_connected() ? "true" : "false",
            (unsigned long)devices,
            (unsigned long)points,
            g_mqtt_host,
            g_mqtt_port,
            g_topic_root,
            g_poll_ms,
            g_discovery_ms,
            g_max_age_sec,
            (long long)unix_time_now());
    fclose(f);
    rename(tmp_path, STATUS_FILE);
}

void status_write_if_due(void)
{
    uint64_t now = monotonic_ms();
    if (now < g_next_status_ms)
        return;
    status_write_now();
    g_next_status_ms = now + 1000;
}

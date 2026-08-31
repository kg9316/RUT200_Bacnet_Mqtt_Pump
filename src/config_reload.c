#include "config_reload.h"
#include "bacnet_client.h"
#include "gateway.h"
#include "mqtt_client.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* uhttpd (uid != root) runs the VuCI config API's Lua handler, which can
 * commit new values to this file but has no permission to restart this
 * (root-owned, procd-managed) daemon - ubus call service set/delete is
 * refused for any non-root caller, and end users only ever have UI access
 * (no SSH), so there's no fallback path that needs a privileged restart
 * at all. The daemon instead watches its own config file's mtime and
 * reloads every runtime knob itself, entirely in-process:
 *  - mqtt_host/mqtt_port: mqtt_client_reconnect() against the new broker
 *  - bacnet_interface: bacnet_client_reinit() tears down and rebinds the
 *    datalink socket on the new interface
 *  - enabled: g_enabled gates whether main()'s loop still calls
 *    bacnet_client_loop() at all - flipping it off pauses discovery,
 *    polling and publishing without killing the process (which would
 *    just get respawned by procd anyway) */

#define CONFIG_FILE "/etc/config/gk_bacnet_mqtt"
#define CONFIG_CHECK_INTERVAL_MS 2000

static time_t g_last_mtime = 0;
static bool g_have_mtime = false;

static void apply_option(const char *name, const char *value)
{
    if (strcmp(name, "mqtt_host") == 0) {
        if (strcmp(g_mqtt_host, value) != 0) {
            safe_copy(g_mqtt_host, sizeof(g_mqtt_host), value);
            mqtt_client_reconnect();
        }
    } else if (strcmp(name, "mqtt_port") == 0) {
        int port = atoi(value);
        if (port > 0 && port != g_mqtt_port) {
            g_mqtt_port = port;
            mqtt_client_reconnect();
        }
    } else if (strcmp(name, "topic_root") == 0) {
        safe_copy(g_topic_root, sizeof(g_topic_root), value);
    } else if (strcmp(name, "poll_ms") == 0) {
        g_poll_ms = (unsigned)strtoul(value, NULL, 10);
    } else if (strcmp(name, "discovery_ms") == 0) {
        g_discovery_ms = (unsigned)strtoul(value, NULL, 10);
    } else if (strcmp(name, "max_age_sec") == 0) {
        g_max_age_sec = (unsigned)strtoul(value, NULL, 10);
    } else if (strcmp(name, "bacnet_interface") == 0) {
        if (strcmp(g_bacnet_interface, value) != 0) {
            safe_copy(g_bacnet_interface, sizeof(g_bacnet_interface), value);
            if (bacnet_client_reinit(g_bacnet_interface) != 0)
                LOG_ERRORF("failed to reinit BACnet datalink on interface=%s", g_bacnet_interface);
        }
    } else if (strcmp(name, "enabled") == 0) {
        g_enabled = (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0);
    }
}

static void reload_from_disk(void)
{
    FILE *f = fopen(CONFIG_FILE, "r");
    char line[512];
    char name[64];
    char value[256];

    if (!f)
        return;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " option %63s '%255[^']'", name, value) == 2)
            apply_option(name, value);
    }

    fclose(f);
    LOG_INFOF("config reloaded from %s (mqtt=%s:%d root=%s poll=%ums discovery=%ums maxAge=%us)",
              CONFIG_FILE, g_mqtt_host, g_mqtt_port, g_topic_root,
              g_poll_ms, g_discovery_ms, g_max_age_sec);
}

void config_reload_init(void)
{
    struct stat st;

    if (stat(CONFIG_FILE, &st) == 0) {
        g_last_mtime = st.st_mtime;
        g_have_mtime = true;
    }
}

void config_reload_if_due(void)
{
    static uint64_t next_check_ms = 0;
    uint64_t now = monotonic_ms();
    struct stat st;

    if (now < next_check_ms)
        return;
    next_check_ms = now + CONFIG_CHECK_INTERVAL_MS;

    if (stat(CONFIG_FILE, &st) != 0)
        return;
    if (g_have_mtime && st.st_mtime == g_last_mtime)
        return;

    g_last_mtime = st.st_mtime;
    g_have_mtime = true;
    reload_from_disk();
}

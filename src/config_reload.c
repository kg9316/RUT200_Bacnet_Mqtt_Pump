#include "config_reload.h"
#include "bacnet_client.h"
#include "gateway.h"
#include "mqtt_client.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <uci.h>

#define CONFIG_FILE "/etc/config/gk_bacnet_mqtt"
static struct stat last_stat;
static bool have_stat;

static const char *option(struct uci_context *ctx, struct uci_section *s,
                          const char *key, const char *fallback)
{
    const char *value = uci_lookup_option_string(ctx, s, key);
    return value ? value : fallback;
}

static void reload_from_disk(bool initial)
{
    struct uci_context *ctx = uci_alloc_context();
    struct uci_package *package = NULL;
    struct uci_section *section;
    MQTT_SETTINGS next = {0};
    char host[sizeof(g_mqtt_host)], interface[sizeof(g_bacnet_interface)];
    int port;
    FILE *config;
    bool mqtt_changed;
    if (!ctx) return;
    /* Read only committed configuration, not staged changes. */
    config = fopen(CONFIG_FILE, "r");
    if (!config) goto done;
    port = uci_import(ctx, config, "gk_bacnet_mqtt", &package, true);
    fclose(config);
    if (port != UCI_OK) goto done;
    section = uci_lookup_section(ctx, package, "main");
    if (!section) goto done;
#define OPT(key, fallback) option(ctx, section, key, fallback)
    if (strlen(OPT("controller_license", "")) >= sizeof(next.license) ||
        strlen(OPT("controller_id", "")) >= sizeof(next.controller_id) ||
        strlen(OPT("mqtt_ca_file", "")) >= sizeof(next.ca_file) ||
        strlen(OPT("mqtt_cert_file", "")) >= sizeof(next.cert_file) ||
        strlen(OPT("mqtt_key_file", "")) >= sizeof(next.key_file) ||
        (strcmp(OPT("mqtt_mode", "generic"), "generic") != 0 &&
         strcmp(OPT("mqtt_mode", "generic"), "gk_cloud") != 0)) {
        LOG_ERRORF("invalid MQTT configuration; retaining previous settings");
        goto done;
    }
    next.tls = strcmp(OPT("mqtt_tls", "0"), "1") == 0;
    next.gk_cloud = strcmp(OPT("mqtt_mode", "generic"), "gk_cloud") == 0;
    safe_copy(next.ca_file, sizeof(next.ca_file), OPT("mqtt_ca_file", MQTT_CA_BUNDLE));
    safe_copy(next.cert_file, sizeof(next.cert_file), OPT("mqtt_cert_file", ""));
    safe_copy(next.key_file, sizeof(next.key_file), OPT("mqtt_key_file", ""));
    safe_copy(next.controller_id, sizeof(next.controller_id), OPT("controller_id", ""));
    safe_copy(next.license, sizeof(next.license), OPT("controller_license", ""));
    safe_copy(host, sizeof(host), OPT("mqtt_host", DEFAULT_MQTT_HOST));
    port = atoi(OPT("mqtt_port", "1883"));
    if (next.gk_cloud) {
        next.tls = true;
        safe_copy(host, sizeof(host), "edge-broker.gkcloud.no");
        port = 8883;
    }
    mqtt_changed = memcmp(&next, &g_mqtt_settings, sizeof(next)) != 0 ||
                   strcmp(host, g_mqtt_host) != 0 || port != g_mqtt_port;
    g_mqtt_settings = next;
    safe_copy(g_mqtt_host, sizeof(g_mqtt_host), host);
    g_mqtt_port = port;
    safe_copy(g_topic_root, sizeof(g_topic_root), OPT("topic_root", "bacnet"));
    g_poll_ms = (unsigned)strtoul(OPT("poll_ms", "5000"), NULL, 10);
    g_discovery_ms = (unsigned)strtoul(OPT("discovery_ms", "10000"), NULL, 10);
    g_max_age_sec = (unsigned)strtoul(OPT("max_age_sec", "300"), NULL, 10);
    g_enabled = strcmp(OPT("enabled", "1"), "1") == 0;
    safe_copy(interface, sizeof(interface), OPT("bacnet_interface", DEFAULT_BACNET_IF));
    if (strcmp(interface, g_bacnet_interface) != 0) {
        safe_copy(g_bacnet_interface, sizeof(g_bacnet_interface), interface);
        if (!initial && bacnet_client_reinit(interface) != 0)
            LOG_ERRORF("failed to reinit BACnet datalink");
    }
    /* Reconnect once, after all settings have been applied. */
    if (!initial && mqtt_changed) mqtt_client_reconnect();
    LOG_INFOF("configuration loaded (MQTT mode=%s TLS=%s)",
              next.gk_cloud ? "GK Cloud" : "generic", next.tls ? "on" : "off");
#undef OPT
 done:
    uci_free_context(ctx);
}

void config_reload_init(void)
{
    if (stat(CONFIG_FILE, &last_stat) == 0) have_stat = true;
    reload_from_disk(true);
}

void config_reload_if_due(void)
{
    static uint64_t next_check_ms;
    uint64_t now = monotonic_ms();
    struct stat st;
    if (now < next_check_ms) return;
    next_check_ms = now + 2000;
    if (stat(CONFIG_FILE, &st) != 0) return;
    if (have_stat && st.st_mtime == last_stat.st_mtime &&
        st.st_mtim.tv_nsec == last_stat.st_mtim.tv_nsec &&
        st.st_ino == last_stat.st_ino && st.st_size == last_stat.st_size) return;
    last_stat = st;
    have_stat = true;
    reload_from_disk(false);
}

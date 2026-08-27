#include "bacnet_client.h"
#include "gateway.h"
#include "logger.h"
#include "mqtt_client.h"
#include "status.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int signal_number)
{
    (void)signal_number;
    g_running = 0;
}

int main(int argc, char **argv)
{
    const char *bacnet_interface = DEFAULT_BACNET_IF;
    int rc;

    if (argc > 1)
        bacnet_interface = argv[1];
    if (argc > 2)
        safe_copy(g_mqtt_host, sizeof(g_mqtt_host), argv[2]);
    if (argc > 3)
        g_mqtt_port = atoi(argv[3]);
    if (argc > 4)
        safe_copy(g_topic_root, sizeof(g_topic_root), argv[4]);
    if (argc > 5)
        g_poll_ms = (unsigned)strtoul(argv[5], NULL, 10);
    if (argc > 6)
        g_discovery_ms = (unsigned)strtoul(argv[6], NULL, 10);
    if (argc > 7)
        g_max_age_sec = (unsigned)strtoul(argv[7], NULL, 10);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    LOG_OPEN();
    LOG_INFOF("starting BACnet=%s MQTT=%s:%d root=%s poll=%ums discovery=%ums maxAge=%us",
              bacnet_interface,
              g_mqtt_host,
              g_mqtt_port,
              g_topic_root,
              g_poll_ms,
              g_discovery_ms,
              g_max_age_sec);

    rc = mqtt_client_init();
    if (rc != 0) {
        LOG_ERRORF("MQTT initialization failed rc=%d", rc);
        LOG_CLOSE();
        return 1;
    }

    if (bacnet_client_init(bacnet_interface) != 0) {
        LOG_ERRORF("BACnet datalink initialization failed interface=%s", bacnet_interface);
        mqtt_client_cleanup();
        LOG_CLOSE();
        return 1;
    }

    status_write_now();

    while (g_running) {
        mqtt_client_loop();
        bacnet_client_loop();
        status_write_if_due();
    }

    LOG_INFOF("stopping");
    bacnet_client_cleanup();
    mqtt_client_cleanup();
    unlink("/tmp/gk-bacnet-mqtt-status.json");
    LOG_CLOSE();
    return 0;
}

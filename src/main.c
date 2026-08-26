#include "bacnet_client.h"
#include "gateway.h"
#include "mqtt_client.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

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

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("RUT200 BACnet -> MQTT Gateway\n");
    printf("BACnet=%s MQTT=%s:%d root=%s\n",
           bacnet_interface,
           g_mqtt_host,
           g_mqtt_port,
           g_topic_root);

    rc = mqtt_client_init();
    if (rc != 0) {
        fprintf(stderr, "MQTT initialization failed rc=%d\n", rc);
        return 1;
    }

    if (bacnet_client_init(bacnet_interface) != 0) {
        fprintf(stderr, "datalink_init failed\n");
        mqtt_client_cleanup();
        return 1;
    }

    while (g_running) {
        mqtt_client_loop();
        bacnet_client_loop();
    }

    bacnet_client_cleanup();
    mqtt_client_cleanup();
    return 0;
}

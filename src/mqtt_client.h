#ifndef RUT200_MQTT_CLIENT_H
#define RUT200_MQTT_CLIENT_H

#include "gateway.h"

#define MQTT_CA_BUNDLE "/etc/ssl/certs/ca-certificates.crt"
typedef struct {
    bool tls;
    bool gk_cloud;
    char ca_file[256];
    char cert_file[256];
    char key_file[256];
    char controller_id[128];
    char license[8192];
} MQTT_SETTINGS;

extern MQTT_SETTINGS g_mqtt_settings;

int mqtt_client_init(void);
void mqtt_client_cleanup(void);
void mqtt_client_loop(void);
void mqtt_client_reconnect(void);
bool mqtt_client_is_connected(void);
void mqtt_publish_config(DEVICE_STATE *device, POINT_STATE *point);
void mqtt_publish_live_if_needed(DEVICE_STATE *device, POINT_STATE *point);

#endif

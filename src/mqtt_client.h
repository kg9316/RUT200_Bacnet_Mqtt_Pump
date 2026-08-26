#ifndef RUT200_MQTT_CLIENT_H
#define RUT200_MQTT_CLIENT_H

#include "gateway.h"

int mqtt_client_init(void);
void mqtt_client_cleanup(void);
void mqtt_client_loop(void);
void mqtt_publish_config(DEVICE_STATE *device, POINT_STATE *point);
void mqtt_publish_live_if_needed(DEVICE_STATE *device, POINT_STATE *point);

#endif

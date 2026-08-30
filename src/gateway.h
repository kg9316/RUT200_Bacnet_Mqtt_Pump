#ifndef RUT200_GATEWAY_H
#define RUT200_GATEWAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "bacnet/bacdef.h"
#include "bacnet/bacenum.h"

#define MAX_DEVICES             64
#define MAX_POINTS_PER_DEVICE  512
#define NAME_LEN                96
#define DESC_LEN               192
#define UNIT_LEN                48
#define TOPIC_LEN              384
#define PAYLOAD_LEN            512

#define DEFAULT_BACNET_IF       "br-lan"
#define DEFAULT_MQTT_HOST       "127.0.0.1"
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_POLL_MS         5000
#define DEFAULT_DISCOVERY_MS    10000
#define DISCOVERY_EMPTY_MS       2000
#define DEFAULT_MAX_AGE_SEC     300
#define DEFAULT_RP_TIMEOUT_MS   3000

#ifndef BACNET_ARRAY_ALL
#define BACNET_ARRAY_ALL 0xFFFFFFFFu
#endif

typedef enum {
    VALUE_NONE = 0,
    VALUE_NUMBER,
    VALUE_BOOL,
    VALUE_STRING
} VALUE_KIND;

typedef struct {
    BACNET_OBJECT_TYPE object_type;
    uint32_t object_instance;
    char name[NAME_LEN];
    char description[DESC_LEN];
    char unit[UNIT_LEN];
    bool metadata_complete;
    bool config_published;
    VALUE_KIND value_kind;
    double numeric_value;
    bool bool_value;
    char string_value[NAME_LEN];
    bool have_value;
    double last_pub_numeric;
    bool last_pub_bool;
    char last_pub_string[NAME_LEN];
    bool have_published_value;
    time_t last_publish;
    uint64_t next_poll_ms;
    uint8_t metadata_step;
} POINT_STATE;

typedef struct {
    bool used;
    uint32_t device_id;
    char name[NAME_LEN];
    bool have_name;
    uint32_t object_count;
    uint32_t next_object_index;
    bool have_object_count;
    bool object_list_complete;
    POINT_STATE points[MAX_POINTS_PER_DEVICE];
    size_t point_count;
    uint64_t last_seen_ms;
} DEVICE_STATE;

typedef enum {
    REQ_NONE = 0,
    REQ_DEVICE_NAME,
    REQ_OBJECT_LIST_COUNT,
    REQ_OBJECT_LIST_ITEM,
    REQ_POINT_NAME,
    REQ_POINT_DESCRIPTION,
    REQ_POINT_UNITS,
    REQ_POINT_PRESENT_VALUE
} REQUEST_KIND;

typedef struct {
    bool active;
    REQUEST_KIND kind;
    uint8_t invoke_id;
    uint64_t sent_ms;
    DEVICE_STATE *device;
    POINT_STATE *point;
    uint32_t object_list_index;
} REQUEST_STATE;

extern DEVICE_STATE g_devices[MAX_DEVICES];
extern REQUEST_STATE g_request;

extern char g_mqtt_host[128];
extern int g_mqtt_port;
extern char g_topic_root[128];
extern unsigned g_poll_ms;
extern unsigned g_discovery_ms;
extern unsigned g_max_age_sec;
extern unsigned g_rp_timeout_ms;

uint64_t monotonic_ms(void);
time_t unix_time_now(void);
void safe_copy(char *dst, size_t n, const char *src);
void topic_sanitize(char *s);
bool double_changed(double a, double b);

#endif

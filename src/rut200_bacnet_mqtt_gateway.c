/*
 * rut200_bacnet_mqtt_gateway.c
 *
 * Native BACnet/IP -> MQTT gateway for Teltonika RUT200 / RutOS.
 * Target: mipsel_24kc/musl, Teltonika libbacnet.so (BACnet Stack 1.3.8),
 *         libmosquitto 2.0.x.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#include <mosquitto.h>

#include "bacnet/bacdef.h"
#include "bacnet/bacenum.h"
#include "bacnet/bacapp.h"
#include "bacnet/apdu.h"
#include "bacnet/npdu.h"
#include "bacnet/rp.h"
#include "bacnet/iam.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/service/h_apdu.h"
#include "bacnet/basic/service/h_iam.h"
#include "bacnet/basic/service/s_whois.h"
#include "bacnet/basic/service/s_rp.h"
#include "bacnet/basic/npdu/h_npdu.h"
#include "bacnet/basic/tsm/tsm.h"

uint32_t gateway_port = 0;
char gateway_address[32] = {0};
uint32_t force_gateway = 0;
uint32_t bbmd_interface = 0;
uint32_t bbmd_enabled = 0;
uint32_t bbmd_port = 47808;

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
#define DEFAULT_MAX_AGE_SEC     300
#define DEFAULT_RP_TIMEOUT_MS   3000

#ifndef BACNET_ARRAY_ALL
#define BACNET_ARRAY_ALL 0xFFFFFFFFu
#endif

static volatile sig_atomic_t g_running = 1;

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static time_t unix_time_now(void) { return time(NULL); }
static void signal_handler(int sig) { (void)sig; g_running = 0; }

static void safe_copy(char *dst, size_t n, const char *src)
{
    if (!dst || !n) return;
    snprintf(dst, n, "%s", src ? src : "");
}

static void topic_sanitize(char *s)
{
    size_t i;
    if (!s) return;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) s[i] = '_';
    }
}

static bool double_changed(double a, double b)
{
    double d = a - b;
    if (d < 0.0) d = -d;
    return d > 0.000001;
}

typedef enum { VALUE_NONE=0, VALUE_NUMBER, VALUE_BOOL, VALUE_STRING } VALUE_KIND;

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

static DEVICE_STATE g_devices[MAX_DEVICES];

typedef enum {
    REQ_NONE=0, REQ_DEVICE_NAME, REQ_OBJECT_LIST_COUNT, REQ_OBJECT_LIST_ITEM,
    REQ_POINT_NAME, REQ_POINT_DESCRIPTION, REQ_POINT_UNITS, REQ_POINT_PRESENT_VALUE
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

static REQUEST_STATE g_request;

static struct mosquitto *g_mosq = NULL;
static bool g_mqtt_connected = false;
static char g_mqtt_host[128] = DEFAULT_MQTT_HOST;
static int g_mqtt_port = DEFAULT_MQTT_PORT;
static char g_topic_root[128] = "bacnet";
static unsigned g_poll_ms = DEFAULT_POLL_MS;
static unsigned g_discovery_ms = DEFAULT_DISCOVERY_MS;
static unsigned g_max_age_sec = DEFAULT_MAX_AGE_SEC;
static unsigned g_rp_timeout_ms = DEFAULT_RP_TIMEOUT_MS;

static void mqtt_on_connect(struct mosquitto *m, void *u, int rc)
{
    (void)m; (void)u;
    g_mqtt_connected = (rc == 0);
    fprintf(rc == 0 ? stdout : stderr, "MQTT %s rc=%d\n", rc == 0 ? "connected" : "connect failed", rc);
}

static void mqtt_on_disconnect(struct mosquitto *m, void *u, int rc)
{
    (void)m; (void)u;
    g_mqtt_connected = false;
    fprintf(stderr, "MQTT disconnected rc=%d\n", rc);
}

static void mqtt_publish_raw(const char *topic, const char *payload, bool retain)
{
    int rc;
    if (!g_mosq || !g_mqtt_connected) return;
    rc = mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 0, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT publish failed: %s\n", mosquitto_strerror(rc));
        g_mqtt_connected = false;
    }
}

static void mqtt_make_point_topic(char *dst, size_t n, const char *branch,
                                  const DEVICE_STATE *dev, const POINT_STATE *p)
{
    char dn[NAME_LEN], pn[NAME_LEN];
    safe_copy(dn, sizeof(dn), dev->have_name ? dev->name : "unknown-device");
    safe_copy(pn, sizeof(pn), p->name[0] ? p->name : "unnamed");
    topic_sanitize(dn); topic_sanitize(pn);
    snprintf(dst, n, "%s/%s/%s/%lu/%s/%u/%lu", g_topic_root, branch, dn,
             (unsigned long)dev->device_id, pn, (unsigned)p->object_type,
             (unsigned long)p->object_instance);
}

static void mqtt_publish_config(DEVICE_STATE *dev, POINT_STATE *p)
{
    char topic[TOPIC_LEN], payload[PAYLOAD_LEN];
    if (!p->metadata_complete) return;
    mqtt_make_point_topic(topic, sizeof(topic), "config", dev, p);
    snprintf(payload, sizeof(payload),
             "{\"unit\":\"%s\",\"description\":\"%s\",\"objectType\":%u,\"objectInstance\":%lu}",
             p->unit, p->description, (unsigned)p->object_type, (unsigned long)p->object_instance);
    mqtt_publish_raw(topic, payload, true);
    if (g_mqtt_connected) p->config_published = true;
}

static void mqtt_publish_live_if_needed(DEVICE_STATE *dev, POINT_STATE *p)
{
    bool changed = false;
    time_t now = unix_time_now();
    bool expired;
    char topic[TOPIC_LEN], payload[PAYLOAD_LEN];
    if (!p->have_value) return;
    expired = !p->have_published_value || !p->last_publish ||
              (unsigned)(now - p->last_publish) >= g_max_age_sec;
    switch (p->value_kind) {
        case VALUE_NUMBER: changed = !p->have_published_value || double_changed(p->numeric_value, p->last_pub_numeric); break;
        case VALUE_BOOL: changed = !p->have_published_value || p->bool_value != p->last_pub_bool; break;
        case VALUE_STRING: changed = !p->have_published_value || strcmp(p->string_value, p->last_pub_string) != 0; break;
        default: return;
    }
    if (!changed && !expired) return;
    mqtt_make_point_topic(topic, sizeof(topic), "status", dev, p);
    if (p->value_kind == VALUE_NUMBER)
        snprintf(payload, sizeof(payload), "{\"value\":%.10g,\"timestamp\":%lld}", p->numeric_value, (long long)now);
    else if (p->value_kind == VALUE_BOOL)
        snprintf(payload, sizeof(payload), "{\"value\":%s,\"timestamp\":%lld}", p->bool_value ? "true" : "false", (long long)now);
    else
        snprintf(payload, sizeof(payload), "{\"value\":\"%s\",\"timestamp\":%lld}", p->string_value, (long long)now);
    mqtt_publish_raw(topic, payload, false);
    if (!g_mqtt_connected) return;
    p->have_published_value = true;
    p->last_publish = now;
    if (p->value_kind == VALUE_NUMBER) p->last_pub_numeric = p->numeric_value;
    else if (p->value_kind == VALUE_BOOL) p->last_pub_bool = p->bool_value;
    else safe_copy(p->last_pub_string, sizeof(p->last_pub_string), p->string_value);
}

static DEVICE_STATE *find_device(uint32_t id)
{
    size_t i;
    for (i=0;i<MAX_DEVICES;i++) if (g_devices[i].used && g_devices[i].device_id == id) return &g_devices[i];
    return NULL;
}

static DEVICE_STATE *get_or_create_device(uint32_t id)
{
    size_t i;
    DEVICE_STATE *d = find_device(id);
    if (d) { d->last_seen_ms = monotonic_ms(); return d; }
    for (i=0;i<MAX_DEVICES;i++) if (!g_devices[i].used) {
        d = &g_devices[i]; memset(d,0,sizeof(*d)); d->used=true; d->device_id=id;
        d->last_seen_ms=monotonic_ms(); d->next_object_index=1;
        printf("NEW DEVICE DEV=%lu\n", (unsigned long)id); return d;
    }
    fprintf(stderr,"Device table full; ignoring %lu\n",(unsigned long)id); return NULL;
}

static bool object_type_has_present_value(BACNET_OBJECT_TYPE t)
{
    switch(t) {
        case OBJECT_ANALOG_INPUT: case OBJECT_ANALOG_OUTPUT: case OBJECT_ANALOG_VALUE:
        case OBJECT_BINARY_INPUT: case OBJECT_BINARY_OUTPUT: case OBJECT_BINARY_VALUE:
        case OBJECT_MULTI_STATE_INPUT: case OBJECT_MULTI_STATE_OUTPUT: case OBJECT_MULTI_STATE_VALUE:
            return true;
        default: return false;
    }
}

static POINT_STATE *add_point(DEVICE_STATE *d, BACNET_OBJECT_TYPE t, uint32_t inst)
{
    size_t i; POINT_STATE *p;
    if (!object_type_has_present_value(t)) return NULL;
    for (i=0;i<d->point_count;i++) if (d->points[i].object_type==t && d->points[i].object_instance==inst) return &d->points[i];
    if (d->point_count >= MAX_POINTS_PER_DEVICE) return NULL;
    p=&d->points[d->point_count++]; memset(p,0,sizeof(*p)); p->object_type=t; p->object_instance=inst; p->next_poll_ms=monotonic_ms(); return p;
}

static bool application_value_to_uint32(const BACNET_APPLICATION_DATA_VALUE *v, uint32_t *out)
{
    if (!v || !out) return false;
    if (v->tag == BACNET_APPLICATION_TAG_UNSIGNED_INT) { *out=v->type.Unsigned_Int; return true; }
    if (v->tag == BACNET_APPLICATION_TAG_ENUMERATED) { *out=v->type.Enumerated; return true; }
    return false;
}

static bool application_value_to_object_id(const BACNET_APPLICATION_DATA_VALUE *v, BACNET_OBJECT_TYPE *t, uint32_t *inst)
{
    if (!v || v->tag != BACNET_APPLICATION_TAG_OBJECT_ID) return false;
    *t=v->type.Object_Id.type; *inst=v->type.Object_Id.instance; return true;
}

static bool application_value_to_string(BACNET_APPLICATION_DATA_VALUE *v, char *dst, size_t n)
{
    size_t len;
    if (!v || !dst || !n || v->tag != BACNET_APPLICATION_TAG_CHARACTER_STRING) return false;
    len=characterstring_length(&v->type.Character_String); if (len>=n) len=n-1;
    memcpy(dst, characterstring_value(&v->type.Character_String), len); dst[len]='\0'; return true;
}

static bool application_value_to_point_value(BACNET_APPLICATION_DATA_VALUE *v, POINT_STATE *p)
{
    if (!v || !p) return false;
    switch(v->tag) {
        case BACNET_APPLICATION_TAG_REAL: p->value_kind=VALUE_NUMBER; p->numeric_value=v->type.Real; return true;
        case BACNET_APPLICATION_TAG_DOUBLE: p->value_kind=VALUE_NUMBER; p->numeric_value=v->type.Double; return true;
        case BACNET_APPLICATION_TAG_UNSIGNED_INT: p->value_kind=VALUE_NUMBER; p->numeric_value=(double)v->type.Unsigned_Int; return true;
        case BACNET_APPLICATION_TAG_SIGNED_INT: p->value_kind=VALUE_NUMBER; p->numeric_value=(double)v->type.Signed_Int; return true;
        case BACNET_APPLICATION_TAG_ENUMERATED: p->value_kind=VALUE_NUMBER; p->numeric_value=(double)v->type.Enumerated; return true;
        case BACNET_APPLICATION_TAG_BOOLEAN: p->value_kind=VALUE_BOOL; p->bool_value=v->type.Boolean; return true;
        case BACNET_APPLICATION_TAG_CHARACTER_STRING: p->value_kind=VALUE_STRING; return application_value_to_string(v,p->string_value,sizeof(p->string_value));
        default: return false;
    }
}

static void unit_to_text(uint32_t u, char *dst, size_t n)
{
    switch(u) {
        case 62: safe_copy(dst,n,"°C"); break; case 64: safe_copy(dst,n,"°F"); break;
        case 98: safe_copy(dst,n,"%"); break; case 95: safe_copy(dst,n,"Pa"); break;
        case 3: safe_copy(dst,n,"A"); break; case 5: safe_copy(dst,n,"V"); break;
        case 47: safe_copy(dst,n,"W"); break; case 48: safe_copy(dst,n,"kW"); break;
        default: snprintf(dst,n,"unit:%lu",(unsigned long)u); break;
    }
}

static void request_clear(void) { memset(&g_request,0,sizeof(g_request)); }

static bool send_read_property(REQUEST_KIND kind, DEVICE_STATE *d, POINT_STATE *p,
                               BACNET_OBJECT_TYPE t, uint32_t inst,
                               BACNET_PROPERTY_ID prop, uint32_t array_index,
                               uint32_t list_index)
{
    uint8_t invoke;
    if (g_request.active || !d) return false;
    invoke=Send_Read_Property_Request(d->device_id,t,inst,prop,array_index);
    if (!invoke) return false;
    memset(&g_request,0,sizeof(g_request)); g_request.active=true; g_request.kind=kind;
    g_request.invoke_id=invoke; g_request.sent_ms=monotonic_ms(); g_request.device=d; g_request.point=p; g_request.object_list_index=list_index;
    return true;
}

static void gateway_i_am_handler(uint8_t *req, uint16_t len, BACNET_ADDRESS *src)
{
    uint32_t device_id=0; unsigned max_apdu=0; int seg=0; uint16_t vendor=0; int rc;
    handler_i_am_bind(req,len,src);
    rc=iam_decode_service_request(req,&device_id,&max_apdu,&seg,&vendor);
    if (rc>=0) get_or_create_device(device_id);
}

static void gateway_read_property_ack_handler(uint8_t *req, uint16_t len, BACNET_ADDRESS *src,
                                               BACNET_CONFIRMED_SERVICE_ACK_DATA *sd)
{
    BACNET_READ_PROPERTY_DATA rp; BACNET_APPLICATION_DATA_VALUE v; int rc;
    (void)src;
    if (!g_request.active || sd->invoke_id != g_request.invoke_id) return;
    memset(&rp,0,sizeof(rp)); memset(&v,0,sizeof(v));
    rc=rp_ack_decode_service_request(req,len,&rp);
    if (rc<=0) { fprintf(stderr,"Malformed RP ACK invoke=%u\n",sd->invoke_id); request_clear(); return; }
    rc=bacapp_decode_application_data(rp.application_data,rp.application_data_len,&v);
    if (rc<=0) { fprintf(stderr,"Cannot decode RP value invoke=%u\n",sd->invoke_id); request_clear(); return; }
    switch(g_request.kind) {
        case REQ_DEVICE_NAME:
            if (application_value_to_string(&v,g_request.device->name,sizeof(g_request.device->name))) {
                g_request.device->have_name=true; printf("DEVICE %lu NAME=%s\n",(unsigned long)g_request.device->device_id,g_request.device->name);
            } break;
        case REQ_OBJECT_LIST_COUNT: {
            uint32_t c; if (application_value_to_uint32(&v,&c)) { g_request.device->object_count=c; g_request.device->have_object_count=true; g_request.device->next_object_index=1; printf("DEVICE %lu OBJECTS=%lu\n",(unsigned long)g_request.device->device_id,(unsigned long)c); }
            break; }
        case REQ_OBJECT_LIST_ITEM: {
            BACNET_OBJECT_TYPE t; uint32_t inst;
            if (application_value_to_object_id(&v,&t,&inst)) {
                if (t!=OBJECT_DEVICE) add_point(g_request.device,t,inst);
                if (g_request.object_list_index >= g_request.device->object_count) g_request.device->object_list_complete=true;
                else g_request.device->next_object_index=g_request.object_list_index+1;
            } break; }
        case REQ_POINT_NAME:
            if (g_request.point) { application_value_to_string(&v,g_request.point->name,sizeof(g_request.point->name)); g_request.point->metadata_step=1; } break;
        case REQ_POINT_DESCRIPTION:
            if (g_request.point) { application_value_to_string(&v,g_request.point->description,sizeof(g_request.point->description)); g_request.point->metadata_step=2; } break;
        case REQ_POINT_UNITS:
            if (g_request.point) { uint32_t u; if (application_value_to_uint32(&v,&u)) unit_to_text(u,g_request.point->unit,sizeof(g_request.point->unit)); g_request.point->metadata_step=3; g_request.point->metadata_complete=true; mqtt_publish_config(g_request.device,g_request.point); } break;
        case REQ_POINT_PRESENT_VALUE:
            if (g_request.point && application_value_to_point_value(&v,g_request.point)) { g_request.point->have_value=true; mqtt_publish_live_if_needed(g_request.device,g_request.point); }
            if (g_request.point) g_request.point->next_poll_ms=monotonic_ms()+g_poll_ms; break;
        default: break;
    }
    request_clear();
}

static void gateway_abort_handler(BACNET_ADDRESS *src, uint8_t invoke, uint8_t reason, bool server)
{
    (void)src;(void)reason;(void)server; if (g_request.active && invoke==g_request.invoke_id) { fprintf(stderr,"BACnet Abort invoke=%u\n",invoke); request_clear(); }
}

static void gateway_reject_handler(BACNET_ADDRESS *src, uint8_t invoke, uint8_t reason)
{
    (void)src;(void)reason; if (g_request.active && invoke==g_request.invoke_id) { fprintf(stderr,"BACnet Reject invoke=%u\n",invoke); request_clear(); }
}

static bool schedule_device_work(DEVICE_STATE *d)
{
    if (!d || !d->used) return false;
    if (!d->have_name) return send_read_property(REQ_DEVICE_NAME,d,NULL,OBJECT_DEVICE,d->device_id,PROP_OBJECT_NAME,BACNET_ARRAY_ALL,0);
    if (!d->have_object_count) return send_read_property(REQ_OBJECT_LIST_COUNT,d,NULL,OBJECT_DEVICE,d->device_id,PROP_OBJECT_LIST,0,0);
    if (!d->object_list_complete) {
        if (!d->next_object_index) d->next_object_index=1;
        if (d->next_object_index>d->object_count) { d->object_list_complete=true; return false; }
        return send_read_property(REQ_OBJECT_LIST_ITEM,d,NULL,OBJECT_DEVICE,d->device_id,PROP_OBJECT_LIST,d->next_object_index,d->next_object_index);
    }
    return false;
}

static bool schedule_metadata(DEVICE_STATE *d, POINT_STATE *p)
{
    if (!d || !p || p->metadata_complete) return false;
    if (p->metadata_step==0) return send_read_property(REQ_POINT_NAME,d,p,p->object_type,p->object_instance,PROP_OBJECT_NAME,BACNET_ARRAY_ALL,0);
    if (p->metadata_step==1) return send_read_property(REQ_POINT_DESCRIPTION,d,p,p->object_type,p->object_instance,PROP_DESCRIPTION,BACNET_ARRAY_ALL,0);
    if (p->metadata_step==2) {
        if (p->object_type==OBJECT_ANALOG_INPUT || p->object_type==OBJECT_ANALOG_OUTPUT || p->object_type==OBJECT_ANALOG_VALUE)
            return send_read_property(REQ_POINT_UNITS,d,p,p->object_type,p->object_instance,PROP_UNITS,BACNET_ARRAY_ALL,0);
        p->unit[0]='\0'; p->metadata_step=3; p->metadata_complete=true; mqtt_publish_config(d,p); return false;
    }
    p->metadata_complete=true; return false;
}

static bool schedule_poll(DEVICE_STATE *d, POINT_STATE *p, uint64_t now)
{
    if (!p->metadata_complete || p->next_poll_ms>now) return false;
    return send_read_property(REQ_POINT_PRESENT_VALUE,d,p,p->object_type,p->object_instance,PROP_PRESENT_VALUE,BACNET_ARRAY_ALL,0);
}

static void scheduler_run(void)
{
    size_t i,j; uint64_t now=monotonic_ms();
    if (g_request.active) {
        if (now-g_request.sent_ms > g_rp_timeout_ms) {
            fprintf(stderr,"BACnet timeout invoke=%u kind=%d\n",g_request.invoke_id,(int)g_request.kind);
            if (g_request.kind==REQ_POINT_UNITS && g_request.point) { g_request.point->unit[0]='\0'; g_request.point->metadata_step=3; g_request.point->metadata_complete=true; mqtt_publish_config(g_request.device,g_request.point); }
            else if (g_request.kind==REQ_POINT_DESCRIPTION && g_request.point) { g_request.point->description[0]='\0'; g_request.point->metadata_step=2; }
            request_clear();
        }
        return;
    }
    for (i=0;i<MAX_DEVICES;i++) if (schedule_device_work(&g_devices[i])) return;
    for (i=0;i<MAX_DEVICES;i++) {
        DEVICE_STATE *d=&g_devices[i]; if (!d->used || !d->object_list_complete) continue;
        for (j=0;j<d->point_count;j++) if (!d->points[j].metadata_complete && schedule_metadata(d,&d->points[j])) return;
    }
    for (i=0;i<MAX_DEVICES;i++) {
        DEVICE_STATE *d=&g_devices[i]; if (!d->used || !d->object_list_complete) continue;
        for (j=0;j<d->point_count;j++) if (schedule_poll(d,&d->points[j],now)) return;
    }
}

int main(int argc, char **argv)
{
    const char *bacnet_if=DEFAULT_BACNET_IF; BACNET_ADDRESS src; uint8_t rx[MAX_MPDU]; uint16_t pdu_len;
    uint64_t next_discovery=0, now; int rc;
    if (argc>1) bacnet_if=argv[1];
    if (argc>2) safe_copy(g_mqtt_host,sizeof(g_mqtt_host),argv[2]);
    if (argc>3) g_mqtt_port=atoi(argv[3]);
    if (argc>4) safe_copy(g_topic_root,sizeof(g_topic_root),argv[4]);
    signal(SIGINT,signal_handler); signal(SIGTERM,signal_handler);
    printf("RUT200 BACnet -> MQTT Gateway\nBACnet=%s MQTT=%s:%d root=%s\n",bacnet_if,g_mqtt_host,g_mqtt_port,g_topic_root);

    rc=mosquitto_lib_init(); if (rc!=MOSQ_ERR_SUCCESS) return 1;
    g_mosq=mosquitto_new("rut200-bacnet-mqtt",true,NULL); if (!g_mosq) return 1;
    mosquitto_connect_callback_set(g_mosq,mqtt_on_connect); mosquitto_disconnect_callback_set(g_mosq,mqtt_on_disconnect);
    mosquitto_reconnect_delay_set(g_mosq,2,30,true);
    mosquitto_connect_async(g_mosq,g_mqtt_host,g_mqtt_port,30);

    address_init();
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM,gateway_i_am_handler);
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROPERTY,gateway_read_property_ack_handler);
    apdu_set_abort_handler(gateway_abort_handler); apdu_set_reject_handler(gateway_reject_handler);
    if (!datalink_init((char *)bacnet_if)) { fprintf(stderr,"datalink_init failed\n"); return 1; }

    while (g_running) {
        now=monotonic_ms();
        if (now>=next_discovery) { Send_WhoIs(-1,-1); next_discovery=now+g_discovery_ms; }
        rc=mosquitto_loop(g_mosq,0,1);
        if (rc!=MOSQ_ERR_SUCCESS && rc!=MOSQ_ERR_NO_CONN) { g_mqtt_connected=false; mosquitto_reconnect_async(g_mosq); }
        memset(&src,0,sizeof(src)); pdu_len=datalink_receive(&src,rx,sizeof(rx),20);
        if (pdu_len) npdu_handler(&src,rx,pdu_len);
        scheduler_run();
        tsm_timer_milliseconds(20);
    }

    datalink_cleanup();
    mosquitto_disconnect(g_mosq); mosquitto_destroy(g_mosq); mosquitto_lib_cleanup();
    return 0;
}

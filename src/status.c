#include "status.h"
#include "bacnet_client.h"
#include "device_table.h"
#include "gateway.h"
#include "mqtt_client.h"
#include "gk_cloud.h"
#include "tag_registry.h"
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

#ifndef STATUS_FILE
#define STATUS_FILE "/tmp/gk-bacnet-mqtt-status.json"
#endif
static uint64_t g_next_status_ms;
#define STR(o,k,v) json_object_object_add(o,k,json_object_new_string((v) ? (v) : ""))
#define NUM(o,k,v) json_object_object_add(o,k,json_object_new_int64((int64_t)(v)))
#define BOOL(o,k,v) json_object_object_add(o,k,json_object_new_boolean(v))

void status_write_now(void)
{
    char tmp_path[160];
    size_t i,count=0;
    uint64_t now=monotonic_ms();
    struct json_object *root=json_object_new_object();
    struct json_object *devices=json_object_new_array();
    FILE *f;
    if (!root) return;
    for(i=0;i<MAX_DEVICES;i++) {
        DEVICE_STATE *d=&g_devices[i];
        if(!d->used) continue;
        struct json_object *row=json_object_new_object();
        NUM(row,"id",d->device_id); STR(row,"name",d->name);
        NUM(row,"points",d->point_count); STR(row,"state",d->state==1?"online":d->state==2?"offline":"unknown");
        NUM(row,"lastResponse",d->last_response); NUM(row,"rpmBatch",d->rpm_limit);
        const char *mode = g_rpm_batch_max==1 ? "single_configured" : d->rpm_unsupported ? "single_unsupported" :
            d->rpm_limit==1 ? "single_fallback" : d->rpm_confirmed ? "multiple" : "pending";
        STR(row,"pollMode",mode);
        NUM(row,"lastPollCount",d->last_poll_count);
        STR(row,"lastPollMode",d->last_poll_count>1?"multiple":d->last_poll_count==1?"single":"unknown");
        tag_registry_set_device_name(d->device_id,d->name);
        NUM(row,"retryIn",d->retry_after_ms>now?(d->retry_after_ms-now+999)/1000:0);
        json_object_array_add(devices,row);
        count += d->point_count;
    }
    tag_registry_flush_metadata();
    json_object_object_add(root,"deviceDetails",devices);
    BOOL(root,"running",true); BOOL(root,"enabled",g_enabled);
    BOOL(root,"mqttConnected",g_enabled && mqtt_client_is_connected());
    BOOL(root,"bacnetActive",g_enabled && g_bacnet_active);
    STR(root,"bacnetState",!g_enabled?"disabled":!g_bacnet_active?"error":g_bacnet_last_reply?"running":"discovering");
    STR(root,"bacnetInterface",g_bacnet_interface);
    STR(root,"bacnetLastError",g_bacnet_error);
    NUM(root,"bacnetRequests",g_bacnet_reads); NUM(root,"bacnetReplies",g_bacnet_replies);
    NUM(root,"bacnetTimeouts",g_bacnet_timeouts); NUM(root,"bacnetErrors",g_bacnet_errors);
    NUM(root,"bacnetLastReply",g_bacnet_last_reply);
    NUM(root,"devices",device_count()); NUM(root,"points",count);
    NUM(root,"rpmBatchMax",g_rpm_batch_max);
    STR(root,"mqttHost",g_mqtt_host); NUM(root,"mqttPort",g_mqtt_port);
    BOOL(root,"mqttTls",g_mqtt_settings.tls); STR(root,"mqttMode",g_mqtt_settings.gk_cloud ? "gk_cloud":"generic");
    STR(root,"topicRoot",g_topic_root);
    NUM(root,"pollMs",g_poll_ms); NUM(root,"discoveryMs",g_discovery_ms); NUM(root,"maxAgeSec",g_max_age_sec);
    NUM(root,"timestamp",unix_time_now());
    NUM(root,"mqttQueued",g_mqtt_diagnostics.queued); NUM(root,"mqttSent",g_mqtt_diagnostics.sent);
    NUM(root,"mqttErrors",g_mqtt_diagnostics.failures); NUM(root,"mqttLastSent",g_mqtt_diagnostics.last_sent);
    STR(root,"mqttLastError",g_mqtt_diagnostics.error);
    STR(root,"authLastError",g_mqtt_settings.gk_cloud ? gk_cloud_last_error() : "");
    snprintf(tmp_path,sizeof(tmp_path),"%s.%ld",STATUS_FILE,(long)getpid());
    f=fopen(tmp_path,"w");
    if(f) {
        int ok=fputs(json_object_to_json_string_ext(root,JSON_C_TO_STRING_PLAIN),f)>=0;
        if(fclose(f)!=0) ok=0;
        if(ok) rename(tmp_path,STATUS_FILE); else unlink(tmp_path);
    }
    json_object_put(root);
}
void status_write_if_due(void)
{
    uint64_t now=monotonic_ms();
    if(now<g_next_status_ms) return;
    status_write_now();
    g_next_status_ms=now+5000;
}

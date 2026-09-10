#include "status.h"
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
    size_t i,j,count=0;
    struct json_object *root=json_object_new_object(), *rows=json_object_new_array();
    FILE *f;
    if (!root || !rows) { if(root) json_object_put(root); if(rows) json_object_put(rows); return; }
    for(i=0;i<MAX_DEVICES;i++) {
        DEVICE_STATE *d=&g_devices[i];
        if(!d->used) continue;
        for(j=0;j<d->point_count;j++) {
            POINT_STATE *p=&d->points[j];
            struct json_object *row=json_object_new_object();
            const char *tag=tag_registry_lookup(d->device_id,(unsigned)p->object_type,p->object_instance);
            ++count;
            STR(row,"t",tag); STR(row,"n",p->name); STR(row,"u",p->unit);
            STR(row,"d",p->description);
            NUM(row,"di",d->device_id); NUM(row,"ot",p->object_type);
            NUM(row,"oi",p->object_instance);
            json_object_array_add(rows,row);
        }
    }
    BOOL(root,"running",true); BOOL(root,"mqttConnected",mqtt_client_is_connected());
    NUM(root,"devices",device_count()); NUM(root,"points",count);
    STR(root,"mqttHost",g_mqtt_host); NUM(root,"mqttPort",g_mqtt_port);
    BOOL(root,"mqttTls",g_mqtt_settings.tls); STR(root,"mqttMode",g_mqtt_settings.gk_cloud ? "gk_cloud":"generic");
    STR(root,"topicRoot",g_topic_root);
    NUM(root,"pollMs",g_poll_ms); NUM(root,"discoveryMs",g_discovery_ms); NUM(root,"maxAgeSec",g_max_age_sec);
    NUM(root,"timestamp",unix_time_now());
    NUM(root,"mqttQueued",g_mqtt_diagnostics.queued); NUM(root,"mqttSent",g_mqtt_diagnostics.sent);
    NUM(root,"mqttErrors",g_mqtt_diagnostics.failures); NUM(root,"mqttLastSent",g_mqtt_diagnostics.last_sent);
    STR(root,"mqttLastError",g_mqtt_diagnostics.error);
    STR(root,"authLastError",g_mqtt_settings.gk_cloud ? gk_cloud_last_error() : "");
    json_object_object_add(root,"pointDetails",rows);
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

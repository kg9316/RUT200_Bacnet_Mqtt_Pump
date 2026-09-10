#include <assert.h>
#include <string.h>
#include <stdlib.h>
#define STATUS_FILE "test-status.json"
#include "../src/status.c"
DEVICE_STATE g_devices[MAX_DEVICES];
MQTT_SETTINGS g_mqtt_settings;
MQTT_DIAGNOSTICS g_mqtt_diagnostics;
char g_mqtt_host[128]="broker\"host", g_topic_root[128]="test\\root";
int g_mqtt_port=8883;
unsigned g_poll_ms=5000,g_discovery_ms=10000,g_max_age_sec=300;
size_t device_count(void) { return 1; }
bool mqtt_client_is_connected(void) { return true; }
const char *gk_cloud_last_error(void) { return "DNS failure"; }
const char *tag_registry_lookup(uint32_t d,unsigned t,uint32_t i) { (void)d;(void)t; return i==0 ? "existing-guid" : NULL; }
uint64_t monotonic_ms(void) { return 0; }
time_t unix_time_now(void) { return 1789036121; }
int main(void) {
    g_devices[0].used=true; g_devices[0].device_id=458967; g_devices[0].point_count=2;
    POINT_STATE *p=&g_devices[0].points[0];
    strcpy(p->name,"Point \"A\"\nline"); strcpy(p->description,"text\\path"); strcpy(p->unit,"deg C");
    p->have_value=true; p->value_kind=VALUE_NUMBER; p->numeric_value=21.5;
    g_devices[0].points[1].object_instance=1;
    g_mqtt_settings.gk_cloud=true; strcpy(g_mqtt_settings.license,"DO-NOT-EXPORT-SECRET");
    g_mqtt_diagnostics.sent=11; g_mqtt_diagnostics.last_sent=1789036100;
    status_write_now();
    struct json_object *root=json_object_from_file(STATUS_FILE), *rows,*row,*v;
    assert(root && json_object_object_get_ex(root,"pointDetails",&rows));
    assert(json_object_array_length(rows)==2);
    row=json_object_array_get_idx(rows,0);
    assert(json_object_object_get_ex(row,"n",&v) && strcmp(json_object_get_string(v),p->name)==0);
    assert(json_object_object_get_ex(row,"t",&v) && strcmp(json_object_get_string(v),"existing-guid")==0);
    assert(!json_object_object_get_ex(row,"value",&v));
    assert(json_object_object_get_ex(row,"d",&v) && strcmp(json_object_get_string(v),p->description)==0);
    assert(json_object_object_get_ex(row,"di",&v) && json_object_get_int(v)==458967);
    row=json_object_array_get_idx(rows,1);
    assert(json_object_object_get_ex(row,"t",&v) && strcmp(json_object_get_string(v),"")==0);
    assert(json_object_object_get_ex(root,"mqttSent",&v) && json_object_get_int(v)==11);
    assert(!strstr(json_object_to_json_string(root),"DO-NOT-EXPORT-SECRET"));
    json_object_put(root); unlink(STATUS_FILE);
    puts("PASS: point/GUID mapping, JSON escaping, pending tags, MQTT counters and secret omission");
}

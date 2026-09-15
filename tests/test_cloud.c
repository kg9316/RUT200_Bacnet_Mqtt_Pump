/* Host regression tests. On Windows only, adapt POSIX file operations to the
 * Win32 equivalents. Production code and JSON/token handling are unchanged. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/gk_cloud.c"

MQTT_SETTINGS g_mqtt_settings;
static uint64_t test_clock = 100000;
uint64_t monotonic_ms(void) { return test_clock; }
uint64_t unix_time_ms(void) { return 1788180633795ULL; }
void safe_copy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src ? src : ""); }

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#include <fcntl.h>
#include <stdarg.h>
#define O_DIRECTORY 0x40000000
static int test_open(const char *path, int flags, ...)
{
    if (strcmp(path, "/dev/urandom") == 0) return 32000;
    if (flags & O_DIRECTORY) return 32001;
    int fd;
    for (int i = 0; i < 100; i++) {
        fd = _open(path, flags | O_BINARY, 0600);
        if (fd >= 0) return fd;
        Sleep(10); /* tolerate host antivirus sharing locks */
    }
    return fd;
}
static int test_read(int fd, void *data, unsigned n)
{
    if (fd == 32000) return BCryptGenRandom(NULL, data, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? (int)n : -1;
    return _read(fd, data, n);
}
static int test_close(int fd) { return fd >= 32000 ? 0 : _close(fd); }
static bool fail_sync;
static unsigned file_syncs;
static int test_fsync(int fd) {
    if (fail_sync) return -1;
    if (fd < 32000) ++file_syncs;
    return fd >= 32000 ? 0 : _commit(fd);
}
static int test_rename(const char *from, const char *to)
{
    for (int i = 0; i < 100; i++) {
        if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
        Sleep(10);
    }
    return -1;
}
#define open test_open
#define read test_read
#define close test_close
#define fsync test_fsync
#define rename test_rename
#endif

#ifndef TAG_REGISTRY_PATH
#define TAG_REGISTRY_PATH "./tags-test.json"
#endif
#include "../src/tag_registry.c"
#include "../src/device_table.c"

static void write_registry(const char *contents)
{
    FILE *f = fopen(TAG_REGISTRY_PATH, "w");
    assert(f);
    fputs(contents, f);
    fclose(f);
}

static void test_guids(void)
{
    char first[37], other[37];
    write_registry("{}");
    assert(tag_registry_init() == 0);
    safe_copy(first, sizeof(first), tag_registry_get(100, 0, 1));
    assert(valid_uuid(first));
    assert(strcmp(first, tag_registry_get(100, 0, 1)) == 0);
    safe_copy(other, sizeof(other), tag_registry_get(100, 1, 1));
    assert(strcmp(first, other) != 0);
    for (unsigned i = 2; i < 50; ++i) assert(tag_registry_get(100, 0, i));
    assert(!tag_registry_is_durable(first));
#ifdef _WIN32
    assert(file_syncs == 0); /* identities are collected without per-point writes */
    fail_sync = true;
    tag_registry_flush_metadata();
    assert(!tag_registry_is_durable(first));
    assert(!strcmp(first,tag_registry_get(100,0,1)));
    fail_sync = false;
    test_clock += 30001;
#endif
    tag_registry_flush_metadata();
    assert(tag_registry_is_durable(first));
#ifdef _WIN32
    assert(file_syncs == 1); /* one durable write for all 50 identities */
#endif
    tag_registry_cleanup();
    assert(tag_registry_init() == 0);
    assert(strcmp(first, tag_registry_get(100, 0, 1)) == 0);
    assert(strcmp(other, tag_registry_get(100, 1, 1)) == 0);
    /* Upgrade a real legacy string entry, retain its UUID after reopening. */
    tag_registry_set_metadata(100, 0, 1, "Room A", "deg C", "Local metadata");
    tag_registry_set_device_name(100, "Controller A");
    tag_registry_set_device_name(200, "Empty controller");
    assert(strcmp(first, tag_registry_get(100, 0, 1)) == 0);
    test_clock += 30001;
    tag_registry_flush_metadata();
    assert(!metadata_dirty);
    tag_registry_set_metadata(100, 0, 1, "Room A", "deg C", "Local metadata");
    assert(!metadata_dirty); /* unchanged metadata must not rewrite flash */
    tag_registry_restore_devices();
    DEVICE_STATE *known=find_device(100);
    assert(known && known->state==0 && known->last_response==0);
    assert(!strcmp(known->points[0].name,"Room A") && !known->points[0].have_value);
    assert(tag_registry_lookup(100,0,1));
    device_table_cleanup();
    assert(tag_registry_is_durable(first));
    tag_registry_flush_metadata();
    assert(tag_registry_is_durable(first));
    tag_registry_cleanup();
    assert(tag_registry_init() == 0);
    assert(strcmp(first, tag_registry_get(100, 0, 1)) == 0);
    assert(strcmp(other, tag_registry_get(100, 1, 1)) == 0);
    struct json_object *entry, *field;
    tag_registry_restore_devices();
    assert(!strcmp(find_device(100)->name, "Controller A"));
    assert(!strcmp(find_device(200)->name, "Empty controller"));
    assert(find_device(200)->state == 0 && find_device(200)->point_count == 0);
    tag_registry_set_device_name(100, "Controller A");
    assert(!metadata_dirty);
    assert(tag_registry_get(200, 0, 1)); /* New GUID with device map present. */
    device_table_cleanup();
    assert(json_object_object_get_ex(registry, "100:0:1", &entry));
    assert(json_object_object_get_ex(entry, "n", &field));
    assert(strcmp(json_object_get_string(field), "Room A") == 0);
    assert(!json_object_object_get_ex(entry, "value", &field));
    DEVICE_STATE labels_device={0}; POINT_STATE labels_point={0};
    labels_device.device_id=100;labels_point.object_type=OBJECT_BINARY_VALUE;labels_point.object_instance=7;
    tag_registry_set_state_text(&labels_device,&labels_point,0,"Off");
    tag_registry_set_state_text(&labels_device,&labels_point,1,"Running \"auto\"");
    char label_guid[37];safe_copy(label_guid,sizeof(label_guid),tag_registry_lookup(100,OBJECT_BINARY_VALUE,7));
    test_clock+=30001;tag_registry_flush_metadata();assert(!metadata_dirty);
    tag_registry_set_state_text(&labels_device,&labels_point,1,"Running \"auto\"");assert(!metadata_dirty);
    tag_registry_cleanup();assert(tag_registry_init()==0);
    assert(!strcmp(label_guid,tag_registry_lookup(100,OBJECT_BINARY_VALUE,7)));
    assert(json_object_object_get_ex(registry,"100:5:7",&entry));
    assert(json_object_object_get_ex(entry,"s",&field));
    assert(json_object_object_get_ex(field,"1",&entry));
    assert(!strcmp(json_object_get_string(entry),"Running \"auto\""));
    tag_registry_cleanup();
    write_registry("broken JSON");
    assert(tag_registry_init() != 0);
    assert(tag_registry_get(100, 0, 1) == NULL);
    write_registry("{\"1:0:1\":\"not-a-guid\"}");
    assert(tag_registry_init() != 0);
    write_registry("{}");
    assert(tag_registry_init() == 0);
}

static void test_messages(void)
{
    DEVICE_STATE *device = calloc(1, sizeof(*device));
    POINT_STATE point = {0};
    char topic[384], original_topic[384];
    const char *types[] = {"number", "enum", "digital", "string"};
    VALUE_KIND kinds[] = {VALUE_NUMBER, VALUE_ENUM, VALUE_BOOL, VALUE_STRING};
    assert(device);
    device->device_id = 100;
    point.object_instance = 1;
    point.have_value = true;
    point.numeric_value = 42;
    point.bool_value = true;
    safe_copy(point.name, sizeof(point.name), "PRIVATE NAME");
    safe_copy(point.description, sizeof(point.description), "PRIVATE DESCRIPTION");
    safe_copy(point.unit, sizeof(point.unit), "PRIVATE UNIT");
    safe_copy(point.string_value, sizeof(point.string_value), "quote\" newline\n slash\\");
    safe_copy(g_mqtt_settings.controller_id, sizeof(g_mqtt_settings.controller_id), "test-controller");
    assert(tag_registry_get(100,0,1));
    assert(gk_cloud_message(device,&point,topic,sizeof(topic)) == NULL);
    test_clock += 30001; tag_registry_flush_metadata();
    for (size_t i = 0; i < 4; i++) {
        struct json_object *root, *value;
        point.value_kind = kinds[i];
        char *payload = gk_cloud_message(device, &point, topic, sizeof(topic));
        assert(payload && !strstr(payload, "PRIVATE"));
        assert(strncmp(topic, "c/test-controller/t/", 20) == 0);
        assert(valid_uuid(topic + 20));
        if (i == 0) safe_copy(original_topic, sizeof(original_topic), topic);
        else assert(strcmp(original_topic, topic) == 0);
        root = json_tokener_parse(payload);
        assert(root && json_object_object_length(root) == 3);
        assert(json_object_object_get_ex(root, "timestamp", &value));
        assert(json_object_get_int64(value) == 1788180633795LL);
        assert(json_object_object_get_ex(root, "dataType", &value));
        assert(strcmp(json_object_get_string(value), types[i]) == 0);
        assert(json_object_object_get_ex(root, i == 3 ? "valueText" : "value", &value));
        if (i == 3) assert(strcmp(json_object_get_string(value), point.string_value) == 0);
        if (i == 1) assert(json_object_is_type(value, json_type_int));
        if (i == 2) assert(json_object_is_type(value, json_type_boolean));
        json_object_put(root);
        free(payload);
    }
    point.value_kind = VALUE_NUMBER;
    point.numeric_value = NAN;
    assert(gk_cloud_message(device, &point, topic, sizeof(topic)) == NULL);
    free(device);
}

static void test_tokens(void)
{
    request_started_ms = test_clock;
    safe_copy(response, sizeof(response), "{\"access_token\":\"test-token\",\"expires_in\":3600,\"token_type\":\"Bearer\"}");
    assert(accept_token());
    assert(strcmp(gk_cloud_token(), "test-token") == 0);
    assert(refresh_ms == request_started_ms + 3540000);
    unsigned before = generation;
    safe_copy(response, sizeof(response), "{\"access_token\":\"bad\",\"expires_in\":0,\"token_type\":\"Bearer\"}");
    assert(!accept_token() && generation == before);
    test_clock = expires_ms;
    assert(gk_cloud_token() == NULL);
    test_clock = 100000;
    request_started_ms = test_clock;
    safe_copy(response, sizeof(response), "{\"access_token\":\"renewed\",\"expires_in\":3600,\"token_type\":\"Bearer\"}");
    assert(accept_token() && generation == before + 1);
    assert(strcmp(gk_cloud_token(), "renewed") == 0);
    response_size = sizeof(response) - 2;
    assert(receive_token("ab", 1, 2, NULL) == 0);
    response_size = 0;
}

static void test_imports(void)
{
    tag_registry_cleanup();device_table_cleanup();write_registry("{}");assert(tag_registry_init()==0);
    safe_copy(g_mqtt_settings.controller_id,sizeof(g_mqtt_settings.controller_id),"controller-A");
    const char *raw="{\"100:5:1\":{\"t\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\",\"n\":\"Fan\",\"s\":{\"0\":\"Off\",\"1\":\"On\"}},\"_dn\":{\"100\":\"Controller A\"}}";
    struct json_object *doc=json_tokener_parse(raw);unsigned added,updated;
    assert(!import_document(doc,&added,&updated) && added==1 && updated==0);
    assert(!strcmp(find_device(100)->name,"Controller A"));
    assert(!import_document(doc,&added,&updated) && added==0 && updated==1);
    json_object_put(doc);
    const char *exported="{\"controllerId\":\"controller-A\",\"points\":[{\"di\":100,\"ot\":5,\"oi\":1,\"t\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\",\"n\":\"Fan restored\"}]}";
    doc=json_tokener_parse(exported);assert(!import_document(doc,&added,&updated));json_object_put(doc);
    struct json_object *point,*states;
    assert(json_object_object_get_ex(registry,"100:5:1",&point));
    assert(json_object_object_get_ex(point,"s",&states)); /* Missing imported fields retain old metadata. */
    const char *bad[]={
      "{\"100:5:1\":\"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb\"}",
      "{\"100:5:2\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\"}",
      "{\"controllerId\":\"wrong-controller\",\"points\":[]}",
      "{\"100:5:2\":{\"t\":\"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb\",\"s\":{\"bad\":\"On\"}}}",
      "{\"100:5:2\":\"not-a-guid\"}", "[]"
    };
    char *before=strdup(json_object_to_json_string_ext(registry,JSON_C_TO_STRING_PLAIN));
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        doc=json_tokener_parse(bad[i]);assert(import_document(doc,&added,&updated));json_object_put(doc);
        assert(!strcmp(before,json_object_to_json_string_ext(registry,JSON_C_TO_STRING_PLAIN)));
    }
    free(before);tag_registry_cleanup();assert(tag_registry_init()==0);
    assert(!strcmp(tag_registry_lookup(100,5,1),"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
    device_table_cleanup();
    puts("PASS: raw/export import, merge preservation, restart persistence, controller/GUID/state validation and rejected imports leave registry unchanged");
}

int main(void)
{
    test_guids();
    test_messages();
    test_tokens();
    test_imports();
    tag_registry_cleanup();
    remove(TAG_REGISTRY_PATH);
    puts("PASS: persistent unique GUIDs, corruption handling, all four payload types, no metadata, token renewal/expiry");
    return 0;
}

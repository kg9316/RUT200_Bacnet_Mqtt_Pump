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
static int test_fsync(int fd) { return fd >= 32000 ? 0 : _commit(fd); }
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
    tag_registry_cleanup();
    assert(tag_registry_init() == 0);
    assert(strcmp(first, tag_registry_get(100, 0, 1)) == 0);
    assert(strcmp(other, tag_registry_get(100, 1, 1)) == 0);
    /* Upgrade a real legacy string entry, retain its UUID after reopening. */
    tag_registry_set_metadata(100, 0, 1, "Room A", "deg C", "Local metadata");
    assert(strcmp(first, tag_registry_get(100, 0, 1)) == 0);
    tag_registry_flush_metadata();
    assert(!metadata_dirty);
    tag_registry_set_metadata(100, 0, 1, "Room A", "deg C", "Local metadata");
    assert(!metadata_dirty); /* unchanged metadata must not rewrite flash */
    tag_registry_cleanup();
    assert(tag_registry_init() == 0);
    assert(strcmp(first, tag_registry_get(100, 0, 1)) == 0);
    assert(strcmp(other, tag_registry_get(100, 1, 1)) == 0);
    struct json_object *entry, *field;
    assert(json_object_object_get_ex(registry, "100:0:1", &entry));
    assert(json_object_object_get_ex(entry, "n", &field));
    assert(strcmp(json_object_get_string(field), "Room A") == 0);
    assert(!json_object_object_get_ex(entry, "value", &field));
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

int main(void)
{
    test_guids();
    test_messages();
    test_tokens();
    tag_registry_cleanup();
    remove(TAG_REGISTRY_PATH);
    puts("PASS: persistent unique GUIDs, corruption handling, all four payload types, no metadata, token renewal/expiry");
    return 0;
}

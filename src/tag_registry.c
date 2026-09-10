#include "tag_registry.h"
#include "logger.h"
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef TAG_REGISTRY_PATH
#define TAG_REGISTRY_PATH "/etc/gk-bacnet-mqtt/tags.json"
#endif
static struct json_object *registry;

static bool valid_uuid(const char *s)
{
    size_t i;
    if (strlen(s) != 36 || s[14] != '4' || !strchr("89ab", s[19])) return false;
    for (i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!strchr("0123456789abcdef", s[i])) return false;
    }
    return true;
}

int tag_registry_init(void)
{
    struct stat st;
    struct json_object *seen;
    if (registry) return 0;
    if (stat(TAG_REGISTRY_PATH, &st) != 0) {
        /* The package installs an empty registry. Never silently replace a
         * missing/corrupt registry: that would change previously issued IDs. */
        LOG_ERRORF("GUID registry missing; restore %s", TAG_REGISTRY_PATH);
        return -1;
    }
    registry = json_object_from_file(TAG_REGISTRY_PATH);
    if (!registry || !json_object_is_type(registry, json_type_object)) goto invalid;
    seen = json_object_new_object();
    if (!seen) goto invalid;
    json_object_object_foreach(registry, key, value) {
        struct json_object *duplicate;
        const char *id = json_object_get_string(value);
        (void)key;
        if (!json_object_is_type(value, json_type_string) || !valid_uuid(id) ||
            strlen(id) != (size_t)json_object_get_string_len(value) ||
            json_object_object_get_ex(seen, id, &duplicate)) {
            json_object_put(seen);
            goto invalid;
        }
        json_object_object_add(seen, id, json_object_new_boolean(true));
    }
    json_object_put(seen);
    return 0;
invalid:
    LOG_ERRORF("GUID registry invalid; restore it before publishing");
    tag_registry_cleanup();
    return -1;
}

void tag_registry_cleanup(void)
{
    if (registry) json_object_put(registry);
    registry = NULL;
}

static bool persist(void)
{
    const char *json = json_object_to_json_string_ext(registry, JSON_C_TO_STRING_PLAIN);
    char tmp[512];
    size_t left = strlen(json);
    int fd, dir;
    snprintf(tmp, sizeof(tmp), "%s.tmp", TAG_REGISTRY_PATH);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    while (left) {
        ssize_t n = write(fd, json, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); unlink(tmp); return false; }
        left -= (size_t)n;
        json += n;
    }
    if (fsync(fd) != 0) { close(fd); unlink(tmp); return false; }
    if (close(fd) != 0 || rename(tmp, TAG_REGISTRY_PATH) != 0) { unlink(tmp); return false; }
    safe_copy(tmp, sizeof(tmp), TAG_REGISTRY_PATH);
    *strrchr(tmp, '/') = 0;
    dir = open(tmp, O_RDONLY | O_DIRECTORY);
    if (dir < 0) return false;
    fd = fsync(dir);
    close(dir);
    return fd == 0;
}

const char *tag_registry_get(uint32_t device, unsigned type, uint32_t instance)
{
    char key[80], id[37];
    unsigned char b[16];
    struct json_object *value;
    int fd;
    size_t got = 0;
    if (!registry) return NULL;
    snprintf(key, sizeof(key), "%lu:%u:%lu", (unsigned long)device, type, (unsigned long)instance);
    if (json_object_object_get_ex(registry, key, &value)) return json_object_get_string(value);
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return NULL;
    while (got < sizeof(b)) {
        ssize_t n = read(fd, b + got, sizeof(b) - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return NULL; }
        got += (size_t)n;
    }
    close(fd);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(id, sizeof(id), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    json_object_object_foreach(registry, existing, item) {
        (void)existing;
        if (strcmp(json_object_get_string(item), id) == 0) return NULL;
    }
    value = json_object_new_string(id);
    if (!value) return NULL;
    json_object_object_add(registry, key, value);
    if (!persist()) {
        LOG_ERRORF("GUID registry could not be saved; publishing suspended");
        /* Do not use an identity until both file and directory are durable. */
        tag_registry_cleanup();
        return NULL;
    }
    return json_object_get_string(value);
}

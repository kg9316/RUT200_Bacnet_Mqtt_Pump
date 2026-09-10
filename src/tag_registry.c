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
static bool metadata_dirty;
static uint64_t next_metadata_flush;

static const char *entry_tag(struct json_object *entry)
{
    struct json_object *tag = entry;
    if (json_object_is_type(entry, json_type_object) &&
        !json_object_object_get_ex(entry, "t", &tag)) return NULL;
    if (!json_object_is_type(tag, json_type_string)) return NULL;
    const char *id = json_object_get_string(tag);
    return strlen(id) == (size_t)json_object_get_string_len(tag) ? id : NULL;
}

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
        const char *id = entry_tag(value);
        (void)key;
        if (!id || !valid_uuid(id) ||
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
    metadata_dirty = false;
    next_metadata_flush = 0;
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
#ifndef _WIN32
    /* Atomic replacements must retain the API reader's group access. */
    struct stat st;
    if (stat(TAG_REGISTRY_PATH, &st) != 0 || fchown(fd, st.st_uid, st.st_gid) != 0 ||
        fchmod(fd, st.st_mode & 0777) != 0) { close(fd); unlink(tmp); return false; }
#endif
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
    if (fd == 0) metadata_dirty = false;
    return fd == 0;
}

const char *tag_registry_lookup(uint32_t device, unsigned type, uint32_t instance)
{
    char key[80];
    struct json_object *value;
    if (!registry) return NULL;
    snprintf(key, sizeof(key), "%lu:%u:%lu", (unsigned long)device, type, (unsigned long)instance);
    return json_object_object_get_ex(registry, key, &value) ? entry_tag(value) : NULL;
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
    if (json_object_object_get_ex(registry, key, &value)) return entry_tag(value);
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
        if (strcmp(entry_tag(item), id) == 0) return NULL;
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
    return entry_tag(value);
}

void tag_registry_set_metadata(uint32_t device, unsigned type, uint32_t instance,
                               const char *name, const char *unit, const char *description)
{
    char key[80];
    struct json_object *entry, *v;
    if (!registry) return;
    snprintf(key, sizeof(key), "%lu:%u:%lu", (unsigned long)device, type, (unsigned long)instance);
    if (!json_object_object_get_ex(registry, key, &entry)) return;
    const char *tag = entry_tag(entry);
    if (!tag) return;
    if (json_object_is_type(entry, json_type_string)) {
        struct json_object *replacement = json_object_new_object();
        if (!replacement) return;
        v = json_object_new_string(tag);
        if (!v) { json_object_put(replacement); return; }
        json_object_object_add(replacement, "t", v);
        json_object_object_add(registry, key, replacement);
        entry = replacement;
        metadata_dirty = true;
    }
    const char *keys[] = {"n", "u", "d"};
    const char *values[] = {name ? name : "", unit ? unit : "", description ? description : ""};
    for (unsigned i = 0; i < 3; i++) {
        if (json_object_object_get_ex(entry, keys[i], &v) &&
            json_object_is_type(v, json_type_string) && strcmp(json_object_get_string(v), values[i]) == 0) continue;
        v = json_object_new_string(values[i]);
        if (!v) continue;
        json_object_object_add(entry, keys[i], v);
        metadata_dirty = true;
    }
}

void tag_registry_flush_metadata(void)
{
    uint64_t now = monotonic_ms();
    if (!registry || !metadata_dirty || now < next_metadata_flush) return;
    next_metadata_flush = now + 30000;
    if (!persist()) LOG_ERRORF("Point metadata could not be saved; retrying in 30 seconds");
}

#include "tag_registry.h"
#include "device_table.h"
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
static struct json_object *pending_ids;
static struct json_object *identity_index;
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
        if (!strcmp(key, "_dn")) {
            if (!json_object_is_type(value, json_type_object)) { json_object_put(seen); goto invalid; }
            json_object_object_foreach(value, device_key, name) {
                unsigned device_id; char extra;
                if (sscanf(device_key, "%u%c", &device_id, &extra) != 1 || device_id > 4194303 ||
                    !json_object_is_type(name, json_type_string)) { json_object_put(seen); goto invalid; }
            }
            continue;
        }
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
    identity_index = seen;
    pending_ids = json_object_new_object();
    if (!pending_ids) goto invalid;
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
    if (pending_ids) json_object_put(pending_ids);
    if (identity_index) json_object_put(identity_index);
    pending_ids = identity_index = NULL;
    metadata_dirty = false;
    next_metadata_flush = 0;
}

static bool persist(void)
{
    const char *json = json_object_to_json_string_ext(registry, JSON_C_TO_STRING_PLAIN);
    char tmp[512];
    if (!json) return false;
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
    if (fd == 0) {
        metadata_dirty = false;
        if (pending_ids) json_object_put(pending_ids);
        pending_ids = NULL;
    }
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
    struct json_object *duplicate;
    if (!identity_index || json_object_object_get_ex(identity_index, id, &duplicate)) return NULL;
    if (!pending_ids) pending_ids = json_object_new_object();
    if (!pending_ids) return NULL;
    value = json_object_new_string(id);
    if (!value) return NULL;
    /* A missing pending marker must never make an unsaved identity publishable.
     * NULL values are sufficient for these sets: membership is tested by key. */
    if (json_object_object_add(pending_ids, id, NULL) != 0 ||
        json_object_object_add(identity_index, id, NULL) != 0 ||
        json_object_object_add(registry, key, value) != 0) {
        json_object_put(value);
        tag_registry_cleanup();
        LOG_ERRORF("Not enough memory to prepare GUID; publishing suspended");
        return NULL;
    }
    metadata_dirty = true;
    return entry_tag(value);
}

bool tag_registry_is_durable(const char *id)
{
    struct json_object *unused;
    return registry && id && (!pending_ids || !json_object_object_get_ex(pending_ids, id, &unused));
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

void tag_registry_set_state_text(DEVICE_STATE *device, POINT_STATE *point, uint32_t state, const char *text)
{
    char address[80], key[16];
    struct json_object *entry, *states, *old;
    if (!text || !tag_registry_get(device->device_id, point->object_type, point->object_instance)) return;
    tag_registry_set_metadata(device->device_id, point->object_type, point->object_instance, point->name, point->unit, point->description);
    snprintf(address, sizeof(address), "%lu:%u:%lu", (unsigned long)device->device_id, (unsigned)point->object_type, (unsigned long)point->object_instance);
    if (!json_object_object_get_ex(registry, address, &entry) || !json_object_is_type(entry,json_type_object)) return;
    if (!json_object_object_get_ex(entry, "s", &states) || !json_object_is_type(states,json_type_object)) {
        states=json_object_new_object();
        if (!states) return;
        json_object_object_add(entry,"s",states);
    }
    snprintf(key,sizeof(key),"%lu",(unsigned long)state);
    if (json_object_object_get_ex(states,key,&old) && json_object_is_type(old,json_type_string) && !strcmp(json_object_get_string(old),text)) return;
    struct json_object *value=json_object_new_string(text);
    if (!value) return;
    json_object_object_add(states,key,value);
    metadata_dirty=true;
}

void tag_registry_set_device_name(uint32_t device, const char *name)
{
    struct json_object *names, *old;
    char key[16];
    if (!registry || !name || !*name) return;
    if (!json_object_object_get_ex(registry, "_dn", &names)) {
        names = json_object_new_object();
        if (!names) return;
        json_object_object_add(registry, "_dn", names);
    }
    snprintf(key, sizeof(key), "%lu", (unsigned long)device);
    if (json_object_object_get_ex(names, key, &old) && !strcmp(json_object_get_string(old), name)) return;
    struct json_object *value = json_object_new_string(name);
    if (!value) return;
    json_object_object_add(names, key, value);
    metadata_dirty = true;
}

void tag_registry_flush_metadata(void)
{
    uint64_t now = monotonic_ms();
    if (!registry || !metadata_dirty || now < next_metadata_flush) return;
    next_metadata_flush = now + 30000;
    if (!persist()) LOG_ERRORF("Point metadata could not be saved; retrying in 30 seconds");
}

/* Restore identities/metadata only. Live values and reachability always start empty. */
void tag_registry_restore_devices(void)
{
    if (!registry) return;
    struct json_object *names;
    if (json_object_object_get_ex(registry, "_dn", &names)) {
        json_object_object_foreach(names, key, name) {
            unsigned id;
            if (sscanf(key, "%u", &id) != 1) continue;
            DEVICE_STATE *d = get_or_create_device(id);
            if (d) safe_copy(d->name, sizeof(d->name), json_object_get_string(name));
        }
    }
    json_object_object_foreach(registry, key, entry) {
        unsigned di, ot, oi; char extra;
        if (sscanf(key, "%u:%u:%u%c", &di, &ot, &oi, &extra) != 3 ||
            di > 4194303 || oi > 4194303 || ot > 1023) continue;
        DEVICE_STATE *d = get_or_create_device(di);
        if (!d) continue;
        POINT_STATE *p = add_point(d, (BACNET_OBJECT_TYPE)ot, oi);
        if (!p || !json_object_is_type(entry, json_type_object)) continue;
        p->metadata_synced = false;
        d->metadata_done = false;
        const char *keys[] = {"n", "u", "d"};
        char *dest[] = {p->name, p->unit, p->description};
        size_t sizes[] = {sizeof(p->name), sizeof(p->unit), sizeof(p->description)};
        for (unsigned i=0; i<3; i++) {
            struct json_object *v;
            if (json_object_object_get_ex(entry, keys[i], &v) && json_object_is_type(v,json_type_string))
                safe_copy(dest[i], sizes[i], json_object_get_string(v));
        }
    }
}

#include "tag_import.inc"

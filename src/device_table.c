#include "device_table.h"

#include <stdio.h>
#include <stdlib.h>
#include "logger.h"
#include <string.h>

DEVICE_STATE g_devices[MAX_DEVICES];
REQUEST_STATE g_request;
static size_t total_points;
static bool total_limit_warned;
/* Open-addressed lookup stores indices, so realloc never invalidates it. */
#define POINT_INDEX_SIZE 32768
typedef struct { uint64_t key; uint16_t index_plus_one; } POINT_INDEX;
static POINT_INDEX *point_index;


DEVICE_STATE *find_device(uint32_t id)
{
    size_t i;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (g_devices[i].used && g_devices[i].device_id == id)
            return &g_devices[i];
    }

    return NULL;
}

DEVICE_STATE *get_or_create_device(uint32_t id)
{
    size_t i;
    DEVICE_STATE *device = find_device(id);

    if (device) {
        /* Discovery is not proof of a successful read. */
        return device;
    }

    for (i = 0; i < MAX_DEVICES; i++) {
        if (!g_devices[i].used) {
            device = &g_devices[i];
            memset(device, 0, sizeof(*device));
            device->used = true;
            device->device_id = id;
            /* Discovery is not proof of a successful read. */
            device->next_object_index = 1;
            printf("NEW DEVICE DEV=%lu\n", (unsigned long)id);
            return device;
        }
    }

    fprintf(stderr, "Device table full; ignoring %lu\n", (unsigned long)id);
    return NULL;
}

size_t device_count(void)
{
    size_t i;
    size_t count = 0;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (g_devices[i].used)
            count++;
    }

    return count;
}

bool object_type_has_present_value(BACNET_OBJECT_TYPE type)
{
    switch (type) {
        case OBJECT_ANALOG_INPUT:
        case OBJECT_ANALOG_OUTPUT:
        case OBJECT_ANALOG_VALUE:
        case OBJECT_BINARY_INPUT:
        case OBJECT_BINARY_OUTPUT:
        case OBJECT_BINARY_VALUE:
        case OBJECT_MULTI_STATE_INPUT:
        case OBJECT_MULTI_STATE_OUTPUT:
        case OBJECT_MULTI_STATE_VALUE:
        case OBJECT_CHARACTERSTRING_VALUE:
        case OBJECT_SCHEDULE:
        case OBJECT_CALENDAR:
            return true;
        default:
            return false;
    }
}

POINT_STATE *add_point(DEVICE_STATE *device, BACNET_OBJECT_TYPE type, uint32_t instance)
{
    POINT_STATE *point;

    if (!object_type_has_present_value(type))
        return NULL;

    if (instance > 4194303 || device->device_id > 4194303) return NULL;
    if (!point_index) point_index = calloc(POINT_INDEX_SIZE, sizeof(*point_index));
    if (!point_index) return NULL;
    uint64_t key = ((uint64_t)device->device_id << 32) | ((uint64_t)(unsigned)type << 22) | instance;
    uint64_t hash = key;
    hash ^= hash >> 33; hash *= UINT64_C(0xff51afd7ed558ccd); hash ^= hash >> 33;
    size_t slot = (size_t)hash & (POINT_INDEX_SIZE-1);
    while (point_index[slot].index_plus_one) {
        if (point_index[slot].key == key) return &device->points[point_index[slot].index_plus_one-1];
        slot = (slot+1) & (POINT_INDEX_SIZE-1);
    }

    if (total_points >= MAX_TOTAL_POINTS) {
        if (!total_limit_warned) LOG_ERRORF("Total BACnet point limit reached: %u", MAX_TOTAL_POINTS);
        total_limit_warned = true;
        return NULL;
    }
    if (device->point_count >= MAX_POINTS_PER_DEVICE)
        return NULL;

    if (device->point_count == device->point_capacity) {
        size_t capacity = device->point_capacity ? device->point_capacity * 2 : 16;
        if (capacity > MAX_POINTS_PER_DEVICE) capacity = MAX_POINTS_PER_DEVICE;
        size_t pending_index = 0;
        bool pending = g_request.device == device && g_request.point != NULL;
        if (pending) pending_index = (size_t)(g_request.point - device->points);
        POINT_STATE *grown = realloc(device->points, capacity * sizeof(*grown));
        if (!grown) {
            if (!device->allocation_warned)
                LOG_ERRORF("Not enough memory for more BACnet points: device=%lu points=%lu",
                           (unsigned long)device->device_id, (unsigned long)device->point_count);
            device->allocation_warned = true;
            return NULL;
        }
        device->points = grown;
        device->point_capacity = capacity;
        device->allocation_warned = false;
        if (pending) g_request.point = &grown[pending_index];
    }

    point_index[slot].key = key;
    point_index[slot].index_plus_one = (uint16_t)(device->point_count+1);
    ++total_points;
    device->metadata_done = false;
    device->values_check_ms = 0;
    point = &device->points[device->point_count++];
    memset(point, 0, sizeof(*point));
    point->object_type = type;
    point->object_instance = instance;
    point->next_poll_ms = monotonic_ms();
    return point;
}

void device_table_cleanup(void)
{
    memset(&g_request, 0, sizeof(g_request));
    for (size_t i = 0; i < MAX_DEVICES; i++) free(g_devices[i].points);
    memset(g_devices, 0, sizeof(g_devices));
    free(point_index); point_index = NULL;
    total_points = 0;
    total_limit_warned = false;
}

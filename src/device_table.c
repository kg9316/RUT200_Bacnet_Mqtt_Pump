#include "device_table.h"

#include <stdio.h>
#include <string.h>

DEVICE_STATE g_devices[MAX_DEVICES];
REQUEST_STATE g_request;

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
        device->last_seen_ms = monotonic_ms();
        return device;
    }

    for (i = 0; i < MAX_DEVICES; i++) {
        if (!g_devices[i].used) {
            device = &g_devices[i];
            memset(device, 0, sizeof(*device));
            device->used = true;
            device->device_id = id;
            device->last_seen_ms = monotonic_ms();
            device->next_object_index = 1;
            printf("NEW DEVICE DEV=%lu\n", (unsigned long)id);
            return device;
        }
    }

    fprintf(stderr, "Device table full; ignoring %lu\n", (unsigned long)id);
    return NULL;
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
            return true;
        default:
            return false;
    }
}

POINT_STATE *add_point(DEVICE_STATE *device, BACNET_OBJECT_TYPE type, uint32_t instance)
{
    size_t i;
    POINT_STATE *point;

    if (!object_type_has_present_value(type))
        return NULL;

    for (i = 0; i < device->point_count; i++) {
        if (device->points[i].object_type == type &&
            device->points[i].object_instance == instance)
            return &device->points[i];
    }

    if (device->point_count >= MAX_POINTS_PER_DEVICE)
        return NULL;

    point = &device->points[device->point_count++];
    memset(point, 0, sizeof(*point));
    point->object_type = type;
    point->object_instance = instance;
    point->next_poll_ms = monotonic_ms();
    return point;
}

#ifndef RUT200_DEVICE_TABLE_H
#define RUT200_DEVICE_TABLE_H

#include "gateway.h"

DEVICE_STATE *find_device(uint32_t id);
DEVICE_STATE *get_or_create_device(uint32_t id);
size_t device_count(void);
bool object_type_has_present_value(BACNET_OBJECT_TYPE type);
POINT_STATE *add_point(DEVICE_STATE *device, BACNET_OBJECT_TYPE type, uint32_t instance);

#endif

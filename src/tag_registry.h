#ifndef TAG_REGISTRY_H
#define TAG_REGISTRY_H
#include "gateway.h"
int tag_registry_init(void);
void tag_registry_cleanup(void);
const char *tag_registry_lookup(uint32_t device, unsigned type, uint32_t instance);
const char *tag_registry_get(uint32_t device, unsigned type, uint32_t instance);
#endif

#ifndef GK_CLOUD_H
#define GK_CLOUD_H

#include "gateway.h"

int gk_cloud_init(void);
void gk_cloud_reset(void);
void gk_cloud_cleanup(void);
void gk_cloud_loop(void);
void gk_cloud_invalidate(void);
const char *gk_cloud_token(void);
unsigned gk_cloud_token_generation(void);
const char *gk_cloud_last_error(void);
/* Caller frees the returned JSON string. NULL means no publishable mapping/value. */
char *gk_cloud_message(const DEVICE_STATE *device, const POINT_STATE *point,
                       char *topic, size_t topic_size);

#endif

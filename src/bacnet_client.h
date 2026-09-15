#ifndef RUT200_BACNET_CLIENT_H
#define RUT200_BACNET_CLIENT_H
#include "gateway.h"
extern bool g_bacnet_active;
extern unsigned long g_bacnet_reads, g_bacnet_replies, g_bacnet_timeouts, g_bacnet_errors;
extern time_t g_bacnet_last_reply;
extern char g_bacnet_error[192];

void bacnet_client_set_rpm_max(unsigned limit);
int bacnet_client_init(const char *interface_name);
int bacnet_client_reinit(const char *interface_name);
void bacnet_client_loop(void);
void bacnet_client_cleanup(void);

#endif

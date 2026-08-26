#ifndef RUT200_BACNET_CLIENT_H
#define RUT200_BACNET_CLIENT_H

int bacnet_client_init(const char *interface_name);
void bacnet_client_loop(void);
void bacnet_client_cleanup(void);

#endif

#ifndef SOLIS_PCNET_H
#define SOLIS_PCNET_H

#include <solis/types.h>

#define PCNET_VENDOR_ID        0x1022
#define PCNET_DEVICE_ID        0x2000
#define PCNET_DEVICE_ID_COMPAT 0x2001

#define PCNET_MAX_FRAME_SIZE 1536

int  pcnet_init(uint16_t io_base);
int  pcnet_send(const uint8_t *data, uint32_t len);
int  pcnet_recv(uint8_t *buf, uint32_t max);
void pcnet_get_mac(uint8_t *mac);

#endif

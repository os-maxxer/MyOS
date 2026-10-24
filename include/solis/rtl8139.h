#ifndef SOLIS_RTL8139_H
#define SOLIS_RTL8139_H

#include <solis/types.h>

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RX_BUF_SIZE 65536
#define TX_BUF_SIZE 1536

int  rtl8139_init(uint16_t io_base);
void rtl8139_send(const uint8_t *data, uint32_t len);
int  rtl8139_recv(uint8_t *buf, uint32_t max);
void rtl8139_get_mac(uint8_t *mac);

#endif

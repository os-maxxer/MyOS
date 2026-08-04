#ifndef SOLIS_PORTS_H
#define SOLIS_PORTS_H

#include <solis/types.h>

void outb(uint16_t port, uint8_t value);
void outw(uint16_t port, uint16_t value);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);
void io_wait(void);

#endif

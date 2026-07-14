#ifndef MYOS_PORTS_H
#define MYOS_PORTS_H

#include <myos/types.h>

void outb(uint16_t port, uint8_t value);
void outw(uint16_t port, uint16_t value);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
void io_wait(void);

#endif

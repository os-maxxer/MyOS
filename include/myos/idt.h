#ifndef MYOS_IDT_H
#define MYOS_IDT_H

#include <myos/types.h>

void idt_init(void);
void isr_handler_register(uint8_t vector, void (*handler)(void));

#endif

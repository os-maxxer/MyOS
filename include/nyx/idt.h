#ifndef NYX_IDT_H
#define NYX_IDT_H

#include <nyx/types.h>

void idt_init(void);
void isr_handler_register(uint8_t vector, void (*handler)(void));
void irq_register_handler(uint8_t irq, void (*handler)(void));

#endif

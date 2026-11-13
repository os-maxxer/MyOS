#ifndef SOLIS_IDT_H
#define SOLIS_IDT_H

#include <solis/types.h>
#include <solis/interrupts.h>

void idt_init(void);
void isr_handler_register(uint8_t vector, void (*handler)(void));
void irq_register_handler(uint8_t irq, void (*handler)(void));
struct registers *isr_common_handler(struct registers *regs);
struct registers *irq_common_handler(struct registers *regs);

#endif

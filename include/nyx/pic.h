#ifndef NYX_PIC_H
#define NYX_PIC_H

#include <nyx/types.h>

void pic_remap(void);
void pic_send_eoi(uint8_t irq);
void pic_enable_irq(uint8_t irq);
void pic_disable_irq(uint8_t irq);

#endif

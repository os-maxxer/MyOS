#ifndef NYX_APIC_H
#define NYX_APIC_H

void apic_init(void);
void apic_timer_init(void);
void apic_send_eoi(void);

#endif

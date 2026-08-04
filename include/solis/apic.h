#ifndef SOLIS_APIC_H
#define SOLIS_APIC_H

void apic_init(void);
void apic_timer_init(void);
void apic_send_eoi(void);

#endif

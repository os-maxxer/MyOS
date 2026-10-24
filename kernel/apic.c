#include <solis/apic.h>
#include <solis/ports.h>

#define APIC_BASE 0xFEE00000

#define APIC_EOI          0x0B0
#define APIC_SPIV         0x0F0
#define APIC_LVT_TIMER    0x320
#define APIC_TIMER_INITCNT 0x380
#define APIC_TIMER_CURRCNT 0x390
#define APIC_TIMER_DIV    0x3E0

static inline uint32_t apic_read(uint32_t off) {
    return *(volatile uint32_t *)(APIC_BASE + off);
}

static inline void apic_write(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(APIC_BASE + off) = val;
}

void apic_init(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    lo |= (1 << 11);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

    apic_write(APIC_SPIV, 0xFF | (1 << 8));
}

void apic_timer_init(void) {
    outb(0x43, 0x30);
    outb(0x40, 11932 & 0xFF);
    outb(0x40, (11932 >> 8) & 0xFF);

    apic_write(APIC_TIMER_DIV, 0);
    apic_write(APIC_TIMER_INITCNT, 0xFFFFFFFF);
    apic_write(APIC_LVT_TIMER, (1 << 16) | (1 << 15));

    uint16_t pit_val;
    do {
        outb(0x43, 0x00);
        pit_val = inb(0x40);
        pit_val |= (uint16_t)inb(0x40) << 8;
    } while (pit_val > 0);

    uint32_t remaining = apic_read(APIC_TIMER_CURRCNT);
    uint32_t ticks = 0xFFFFFFFF - remaining;

    apic_write(APIC_TIMER_DIV, 0);
    apic_write(APIC_TIMER_INITCNT, ticks);
    apic_write(APIC_LVT_TIMER, 32 | (1 << 16));
}

void apic_send_eoi(void) {
    apic_write(APIC_EOI, 0);
}

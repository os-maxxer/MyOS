#include <nyx/timer.h>
#include <nyx/ports.h>
#include <nyx/idt.h>
#include <nyx/pic.h>

#define PIT_COMMAND  0x43
#define PIT_CHANNEL0 0x40
#define PIT_FREQ     1193182

static volatile uint32_t tick_count = 0;

static void pit_handler(void) {
    tick_count++;
}

void timer_init(void) {
    uint32_t divisor = PIT_FREQ / 100;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    irq_register_handler(0, pit_handler);
    pic_enable_irq(0);
}

uint32_t timer_get_ticks(void) {
    return tick_count;
}

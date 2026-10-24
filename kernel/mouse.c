#include <solis/mouse.h>
#include <solis/ports.h>
#include <solis/pic.h>
#include <solis/idt.h>
#include <solis/dbg.h>

static volatile int32_t mouse_dx = 0;
static volatile int32_t mouse_dy = 0;
static volatile uint8_t mouse_buttons = 0;
static volatile bool mouse_updated = false;

static int mouse_cycle = 0;
static uint8_t mouse_byte[3];

static void mouse_handler(void) {
    uint8_t data = inb(0x60);

    switch (mouse_cycle) {
        case 0:
            if (!(data & 0x08)) {
                pic_send_eoi(12);
                return;
            }
            mouse_byte[0] = data;
            mouse_cycle = 1;
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_byte[2] = data;
            mouse_cycle = 0;
            {
                int32_t dx = (int32_t)(int8_t)mouse_byte[1];
                int32_t dy = -(int32_t)(int8_t)mouse_byte[2];
                if (mouse_byte[0] & 0x40) dy = (mouse_byte[0] & 0x80) ? -127 : 127;
                if (mouse_byte[0] & 0x20) dx = (mouse_byte[0] & 0x10) ? -127 : 127;
                if (dx || dy) {
                    mouse_dx += dx;
                    mouse_dy += dy;
                }
                mouse_buttons = mouse_byte[0] & 0x07;
                mouse_updated = true;
            }
            break;
    }
    pic_send_eoi(12);
}

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (!(inb(0x64) & 0x02)) return;
        }
    } else {
        while (timeout--) {
            if (inb(0x64) & 0x01) return;
        }
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait(0);
    outb(0x64, 0xD4);
    io_wait();
    mouse_wait(0);
    outb(0x60, data);
    io_wait();
}

static uint8_t mouse_read(void) {
    mouse_wait(1);
    return inb(0x60);
}

void mouse_init(void) {
    mouse_wait(0);
    outb(0x64, 0xA8);
    io_wait();
    mouse_wait(0);
    outb(0x64, 0x20);
    io_wait();
    mouse_wait(1);
    uint8_t status = inb(0x60);
    status |= 0x02 | 0x20;
    mouse_wait(0);
    outb(0x64, 0x60);
    io_wait();
    mouse_wait(0);
    outb(0x60, status);
    io_wait();

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xE8);
    mouse_read();
    mouse_write(0x01);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();

    uint32_t flush_timeout = 10000;
    while (flush_timeout--) {
        if (inb(0x64) & 0x01) {
            inb(0x60);
        } else {
            break;
        }
    }

    mouse_cycle = 0;

    dbg_set_driver_state("mouse", DBG_DRIVER_ACTIVE);
    irq_register_handler(12, mouse_handler);
    pic_enable_irq(12);
}

void mouse_get_state(struct mouse_state *state) {
    if (state != 0) {
        int32_t dx = mouse_dx;
        int32_t dy = mouse_dy;
        dx = (dx > 0) ? (dx / 2 + 1) : (dx / 2);
        dy = (dy > 0) ? (dy / 2 + 1) : (dy / 2);
        state->dx = dx;
        state->dy = dy;
        state->buttons = mouse_buttons;
        state->present = true;
        mouse_dx = 0;
        mouse_dy = 0;
    }
}

bool mouse_has_moved(void) {
    return mouse_updated;
}

void mouse_clear_moved(void) {
    mouse_updated = false;
}

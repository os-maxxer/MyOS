#include <solis/mouse.h>
#include <solis/ports.h>
#include <solis/pic.h>
#include <solis/idt.h>
#include <solis/dbg.h>

static volatile int32_t mouse_dx = 0;
static volatile int32_t mouse_dy = 0;
static volatile int32_t mouse_wheel = 0;
static volatile uint8_t mouse_buttons = 0;
static volatile bool mouse_updated = false;
static bool mouse_has_wheel = false;

static int mouse_cycle = 0;
static uint8_t mouse_byte[4];

/* Pointer speed as 16.16 fixed point, applied in mouse_get_state(). The
 * sub-pixel remainder is carried between polls so slow movement is neither
 * truncated away nor rounded up, which is what made small motions stutter. */
#define MOUSE_SENS_ONE 0x10000
static uint32_t mouse_sens = MOUSE_SENS_ONE;
static int32_t mouse_frac_x = 0;
static int32_t mouse_frac_y = 0;

#define PS2_STATUS     0x64
#define PS2_DATA       0x60
#define PS2_STATUS_OBF 0x01
#define PS2_STATUS_AUX 0x20
#define PS2_ACK        0xFA

/* PS/2 status byte (first byte of every 3-byte packet):
 *   bit 0-2  button state
 *   bit 3    always 1 (packet sync)
 *   bit 4    X sign
 *   bit 5    Y sign
 *   bit 6    X overflow
 *   bit 7    Y overflow
 * Signs are already handled by the (int8_t) casts, and an overflowed axis
 * carries no usable value, so both axes are simply dropped when their
 * overflow bit is set. Synthesising +/-127 instead is what made the pointer
 * teleport across the screen. */
/* Feed one AUX byte through the 3-byte packet state machine. */
static void mouse_consume(uint8_t data) {
    /* Command acknowledgements share the AUX stream. Treat one as a
     * resync point so it can never be mistaken for a packet header. */
    if (data == PS2_ACK) {
        mouse_cycle = 0;
        return;
    }

    switch (mouse_cycle) {
        case 0:
            if (data & 0x08) {
                mouse_byte[0] = data;
                mouse_cycle = 1;
            } else {
                /* Not a packet header: drop it and stay aligned on the next
                 * byte, so a stray byte cannot shift every later packet. */
                mouse_cycle = 0;
            }
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_byte[2] = data;
            /* Intellimouse adds a 4th byte carrying the wheel. Reading it
             * unconditionally keeps the packet phase aligned whether or not
             * the device actually sends one. */
            mouse_cycle = 3;
            break;
        case 3:
            mouse_byte[3] = data;
            mouse_cycle = 0;
            {
                int32_t dx = (int32_t)(int8_t)mouse_byte[1];
                int32_t dy = -(int32_t)(int8_t)mouse_byte[2];
                if (mouse_byte[0] & 0x40) dx = 0;  /* X overflow */
                if (mouse_byte[0] & 0x80) dy = 0;  /* Y overflow */
                if (dx || dy) {
                    mouse_dx += dx;
                    mouse_dy += dy;
                    mouse_updated = true;
                }
                if (mouse_has_wheel) {
                    /* 4-bit two's complement in the low nibble, with the
                     * upper bits repeating the sign. */
                    int8_t wheel = (int8_t)(mouse_byte[3] & 0x0F);
                    if (wheel & 0x08) wheel = (int8_t)(wheel | 0xF0);
                    if (wheel) {
                        mouse_wheel += wheel;
                        mouse_updated = true;
                    }
                }
                mouse_buttons = mouse_byte[0] & 0x07;
            }
            break;
    }
}

static void mouse_handler(void) {
    /* Drain the whole output buffer instead of reading a single byte and
     * returning. A packet is three bytes and the controller can queue them
     * faster than the interrupt is redelivered; consuming one byte per IRQ
     * leaves the rest sitting in the buffer with no further edge to wake the
     * handler, which strands the state machine mid-packet and silently kills
     * the pointer (and can commit a half-decoded packet). */
    for (int guard = 0; guard < 32; guard++) {
        uint8_t status = inb(PS2_STATUS);
        if (!(status & PS2_STATUS_OBF)) break;
        /* Without the AUX bit the byte belongs to the keyboard, and IRQ1
         * owns it. Leave it in place. */
        if (!(status & PS2_STATUS_AUX)) break;
        mouse_consume(inb(PS2_DATA));
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
    /* Keep IRQ12 masked for the whole setup sequence: enabling the aux port
     * makes the controller raise it for the acknowledgements and config byte
     * exchanged below, and those bytes would otherwise be parsed as motion
     * packets the first time the handler runs. */
    pic_disable_irq(12);

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

    /* Enable the scroll wheel: sample rates 200/100/80 followed by a device
     * ID read. A wheel mouse answers 0x03/0x04 and switches to 4-byte
     * packets; a plain mouse answers 0x00. The magic sequence is harmless
     * on a plain mouse, so it is sent unconditionally. */
    static const uint8_t wheel_magic[] = {0xF3, 0xC8, 0xF3, 0x64, 0xF3, 0x50};
    for (unsigned i = 0; i < sizeof(wheel_magic); i++) {
        mouse_write(wheel_magic[i]);
        mouse_read();
    }
    mouse_write(0xF2);
    mouse_read();
    uint8_t id = mouse_read();
    mouse_has_wheel = (id == 0x03 || id == 0x04);

    mouse_write(0xF3);
    mouse_read();
    mouse_write(0x64);   /* 100 samples/sec: a sane IRQ rate */
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
        int32_t raw_x = mouse_dx;
        int32_t raw_y = mouse_dy;
        mouse_dx = 0;
        mouse_dy = 0;

        /* Apply sensitivity in fixed point and carry the remainder, so slow
         * motion accumulates instead of being truncated to zero (which made
         * the pointer feel laggy) and small steps are not inflated (which
         * made it jitter). Rounding is symmetric about zero, so the pointer
         * no longer drifts towards one direction. */
        int64_t acc_x = (int64_t)raw_x * (int64_t)mouse_sens + mouse_frac_x;
        int64_t acc_y = (int64_t)raw_y * (int64_t)mouse_sens + mouse_frac_y;

        int32_t out_x = (int32_t)(acc_x / MOUSE_SENS_ONE);
        int32_t out_y = (int32_t)(acc_y / MOUSE_SENS_ONE);

        mouse_frac_x = (int32_t)(acc_x - (int64_t)out_x * MOUSE_SENS_ONE);
        mouse_frac_y = (int32_t)(acc_y - (int64_t)out_y * MOUSE_SENS_ONE);

        state->dx = out_x;
        state->dy = out_y;
        state->buttons = mouse_buttons;
        state->present = true;
    }
}

int32_t mouse_take_wheel(void) {
    int32_t w = mouse_wheel;
    mouse_wheel = 0;
    return w;
}

void mouse_set_sensitivity(uint32_t fixed_16_16) {
    if (fixed_16_16 < 0x2000) fixed_16_16 = 0x2000;   /* floor at 1/8x */
    if (fixed_16_16 > 0x40000) fixed_16_16 = 0x40000; /* cap at 4x    */
    mouse_sens = fixed_16_16;
}

uint32_t mouse_get_sensitivity(void) {
    return mouse_sens;
}

bool mouse_has_moved(void) {
    return mouse_updated;
}

void mouse_clear_moved(void) {
    mouse_updated = false;
}

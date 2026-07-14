/*
 * Keyboard driver for PS/2 keyboards.
 */

#include <myos/keyboard.h>
#include <myos/ports.h>
#include <myos/pic.h>
#include <myos/console.h>

static volatile bool keyboard_ready = false;
static volatile uint8_t keyboard_buffer[256];
static volatile uint8_t keyboard_head = 0;
static volatile uint8_t keyboard_tail = 0;

void keyboard_init(void) {
    keyboard_ready = true;
    pic_enable_irq(1);
    pic_send_eoi(1);
}

bool keyboard_has_input(void) {
    return keyboard_head != keyboard_tail;
}

bool keyboard_read_char(char *out) {
    if (!keyboard_has_input()) {
        return false;
    }
    *out = (char)keyboard_buffer[keyboard_tail];
    keyboard_tail = (keyboard_tail + 1) % 256;
    return true;
}

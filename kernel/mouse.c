/*
 * PS/2 mouse driver placeholder implementation.
 */

#include <myos/mouse.h>
#include <myos/ports.h>
#include <myos/pic.h>

static struct mouse_state current_state = {0, 0, 0, false};

void mouse_init(void) {
    current_state.present = true;
    pic_enable_irq(12);
    pic_send_eoi(12);
}

void mouse_get_state(struct mouse_state *state) {
    if (state != 0) {
        *state = current_state;
    }
}

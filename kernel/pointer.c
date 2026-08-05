#include <solis/pointer.h>
#include <solis/mouse.h>
#include <solis/touchpad.h>
#include <solis/i2c.h>
#include <solis/timer.h>

#define TOUCH_POLL_INTERVAL 3

static bool touchpad_active = false;
static uint32_t last_touch_poll = 0;

void pointer_init(void) {
    /* PS/2 mouse first - works on every i8042-equipped machine (incl. QEMU).
     * Then probe for an I2C HID touchpad, which only exists when an SMBus
     * controller is present (modern laptops). */
    mouse_init();
    i2c_init();
    touchpad_init();
    touchpad_active = touchpad_present();
}

bool pointer_poll(struct pointer_state *state) {
    if (!state) return false;

    state->dx = 0;
    state->dy = 0;
    state->buttons = 0;
    state->present = true;
    state->is_touchpad = touchpad_active;

    struct mouse_state ms;
    mouse_get_state(&ms);
    state->dx += ms.dx;
    state->dy += ms.dy;
    state->buttons |= ms.buttons;

    if (touchpad_active) {
        /* SMBus transactions are slow and the touchpad is polled, so don't
         * hammer the bus on every loop iteration. */
        uint32_t now = timer_get_ticks();
        if (now - last_touch_poll >= TOUCH_POLL_INTERVAL) {
            last_touch_poll = now;
            struct touchpad_state ts;
            if (touchpad_poll(&ts)) {
                state->dx += ts.dx;
                state->dy += ts.dy;
                state->buttons |= ts.buttons;
            }
        }
    }

    return state->dx != 0 || state->dy != 0 || state->buttons != 0;
}

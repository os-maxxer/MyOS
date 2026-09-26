#ifndef SOLIS_POINTER_H
#define SOLIS_POINTER_H

#include <stdbool.h>
#include <solis/types.h>

/* Unified pointer input: merges a PS/2 mouse (legacy) and an I2C HID
 * touchpad (newer laptops). Exactly one of the two is usually present,
 * but both are supported at the same time if a machine has both. */

struct pointer_state {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    bool present;
    bool is_touchpad;
};

void pointer_init(void);
bool pointer_poll(struct pointer_state *state);

#endif

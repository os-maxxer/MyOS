#ifndef SOLIS_TOUCHPAD_H
#define SOLIS_TOUCHPAD_H

#include <solis/types.h>
#include <stdbool.h>

struct touchpad_state {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    bool present;
};

void touchpad_init(void);
bool touchpad_poll(struct touchpad_state *state);
bool touchpad_present(void);

#endif

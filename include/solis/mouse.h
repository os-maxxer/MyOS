#ifndef SOLIS_MOUSE_H
#define SOLIS_MOUSE_H

#include <stdbool.h>
#include <solis/types.h>

struct mouse_state {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    bool present;
};

void mouse_init(void);
void mouse_get_state(struct mouse_state *state);
bool mouse_has_moved(void);
void mouse_clear_moved(void);

/* Pointer speed as a 16.16 fixed-point multiplier (0x10000 == 1.0x).
 * Sub-pixel movement is carried between polls, so values below 1.0x stay
 * smooth instead of being rounded away. */
void mouse_set_sensitivity(uint32_t fixed_16_16);
uint32_t mouse_get_sensitivity(void);

/* Accumulated scroll-wheel notches since the last call, positive up. */
int32_t mouse_take_wheel(void);

#endif

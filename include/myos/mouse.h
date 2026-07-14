#ifndef MYOS_MOUSE_H
#define MYOS_MOUSE_H

#include <stdbool.h>
#include <myos/types.h>

struct mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons;
    bool present;
};

void mouse_init(void);
void mouse_get_state(struct mouse_state *state);

#endif

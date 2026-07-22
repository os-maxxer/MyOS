#ifndef NYX_MOUSE_H
#define NYX_MOUSE_H

#include <stdbool.h>
#include <nyx/types.h>

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

#endif

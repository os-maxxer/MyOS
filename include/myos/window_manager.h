#ifndef MYOS_WINDOW_MANAGER_H
#define MYOS_WINDOW_MANAGER_H

#include <myos/gui.h>

void window_manager_init(void);
void window_manager_redraw(void);
void window_manager_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons);
void window_manager_handle_key(char key);

#endif

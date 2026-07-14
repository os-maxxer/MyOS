#ifndef MYOS_GUI_H
#define MYOS_GUI_H

#include <myos/types.h>
#include <stdbool.h>

struct gui_point { int32_t x; int32_t y; };
struct gui_rect { int32_t x; int32_t y; int32_t width; int32_t height; };

void gui_init(void);
void gui_redraw(void);
void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons);
void gui_handle_key(char key);

#endif

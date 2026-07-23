#ifndef NYX_APPS_SETTINGS_H
#define NYX_APPS_SETTINGS_H

#include <nyx/types.h>

void settings_init(void);
void settings_draw(int x, int y, int w, int h);
void settings_handle_key(char key);
void settings_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y);

#endif

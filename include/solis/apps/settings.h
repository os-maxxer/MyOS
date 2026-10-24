#ifndef SOLIS_APPS_SETTINGS_H
#define SOLIS_APPS_SETTINGS_H

#include <solis/types.h>

void settings_init(void);
void settings_draw(int x, int y, int w, int h);
void settings_handle_key(char key);
void settings_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y);
void settings_handle_scroll(int x, int y, int w, int h, int notches);

#endif

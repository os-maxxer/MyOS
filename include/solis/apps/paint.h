#ifndef SOLIS_APPS_PAINT_H
#define SOLIS_APPS_PAINT_H

void paint_init(void);
void paint_draw(int x, int y, int w, int h);
void paint_handle_key(char key);
void paint_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y);

#endif

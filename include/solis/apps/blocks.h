#ifndef SOLIS_APPS_BLOCKS_H
#define SOLIS_APPS_BLOCKS_H

void blocks_init(void);
void blocks_draw(int x, int y, int w, int h);
void blocks_handle_key(char key);
void blocks_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y);

#endif
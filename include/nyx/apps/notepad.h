#ifndef NYX_APPS_NOTEPAD_H
#define NYX_APPS_NOTEPAD_H

void notepad_init(void);
void notepad_draw(int x, int y, int w, int h);
void notepad_handle_key(char key);
void notepad_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y);

#endif
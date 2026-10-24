#ifndef SOLIS_APPS_FILEBROWSER_H
#define SOLIS_APPS_FILEBROWSER_H

void filebrowser_init(void);
void filebrowser_draw(int x, int y, int w, int h);
void filebrowser_handle_key(char key);
void filebrowser_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y);
void filebrowser_handle_context(int x, int y, int w, int h, int mouse_x, int mouse_y);

#endif

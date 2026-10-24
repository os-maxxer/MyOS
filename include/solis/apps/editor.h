#ifndef SOLIS_APPS_EDITOR_H
#define SOLIS_APPS_EDITOR_H

void editor_init(void);
void editor_draw(int x, int y, int w, int h);
void editor_handle_key(char key);
void editor_handle_mouse(int x, int y, int w, int h, int mx, int my);
void editor_open_file(const char *path);

#endif

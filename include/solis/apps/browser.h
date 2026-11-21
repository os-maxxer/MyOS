#ifndef SOLIS_APPS_BROWSER_H
#define SOLIS_APPS_BROWSER_H

void browser_init(void);
void browser_draw(int x, int y, int w, int h);
void browser_handle_key(char key);
void browser_handle_mouse(int x, int y, int w, int h, int mx, int my);
void browser_handle_scroll(int x, int y, int w, int h, int notches);
void browser_get_size(int *w, int *h);
int  browser_tick(void);
void browser_open_file(const char *path);

#endif

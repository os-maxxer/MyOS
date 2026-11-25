#ifndef SOLIS_APPS_CK3_H
#define SOLIS_APPS_CK3_H

void ck3_init(void);
void ck3_draw(int x, int y, int w, int h);
void ck3_handle_key(char key);
void ck3_handle_mouse(int x, int y, int w, int h, int mx, int my);
void ck3_handle_context(int x, int y, int w, int h, int mx, int my);
void ck3_handle_scroll(int x, int y, int w, int h, int notches);
int  ck3_tick(void);
void ck3_get_size(int *w, int *h);

#endif

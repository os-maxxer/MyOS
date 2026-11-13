#ifndef SOLIS_APPS_CALCULATOR_H
#define SOLIS_APPS_CALCULATOR_H

void calculator_init(void);
void calculator_draw(int x, int y, int w, int h);
void calculator_handle_key(char key);
void calculator_handle_mouse(int x, int y, int w, int h, int mx, int my);
void calculator_get_size(int *w, int *h);

#endif

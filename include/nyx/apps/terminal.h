#ifndef NYX_APPS_TERMINAL_H
#define NYX_APPS_TERMINAL_H

void term_init(void);
void term_draw(int x, int y, int w, int h);
void term_handle_key(char key);

#endif

#ifndef SOLIS_APPS_TETRIS_H
#define SOLIS_APPS_TETRIS_H

void tetris_init(void);
int  tetris_tick(void);
void tetris_draw(int x, int y, int w, int h);
void tetris_handle_key(char key);

#endif

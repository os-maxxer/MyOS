#include <solis/spx.h>

extern void taskman_init(void);
extern void taskman_draw(int x, int y, int w, int h);
extern void taskman_handle_key(char key);

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Task Manager",
    .init = taskman_init,
    .draw = taskman_draw,
    .handle_key = taskman_handle_key,
    .handle_mouse = 0,
};

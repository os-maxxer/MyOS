#include <solis/spx.h>

extern void pkg_init(void);
extern void pkg_draw(int x, int y, int w, int h);
extern void pkg_handle_key(char key);

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "SOLPKG",
    .init = pkg_init,
    .draw = pkg_draw,
    .handle_key = pkg_handle_key,
    .handle_mouse = 0,
};

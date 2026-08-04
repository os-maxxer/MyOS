#include <solis/spx.h>
#include <solis/apps/paint.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Paint",
    .init = paint_init,
    .draw = paint_draw,
    .handle_key = paint_handle_key,
    .handle_mouse = 0,
};

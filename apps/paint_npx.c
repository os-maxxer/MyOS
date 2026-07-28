#include <nyx/npx.h>
#include <nyx/apps/paint.h>

const struct app_exports __attribute__((section(".npx_exports"), used)) npx_app = {
    .name = "Paint",
    .init = paint_init,
    .draw = paint_draw,
    .handle_key = paint_handle_key,
    .handle_mouse = 0,
};

#include <solis/spx.h>
#include <solis/apps/ck3.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "CK3",
    .init = ck3_init,
    .draw = ck3_draw,
    .handle_key = ck3_handle_key,
    .handle_mouse = ck3_handle_mouse,
    .handle_context = ck3_handle_context,
    .handle_scroll = ck3_handle_scroll,
    .tick = ck3_tick,
    .get_size = ck3_get_size,
};

#include <solis/spx.h>
#include <solis/apps/ck3.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "CK3",
    .init = ck3_init,
    .draw = ck3_draw,
    .handle_key = ck3_handle_key,
    .handle_mouse = 0,
    .get_size = ck3_get_size,
};

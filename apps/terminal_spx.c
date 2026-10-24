#include <solis/spx.h>
#include <solis/apps/terminal.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Helios",
    .init = term_init,
    .draw = term_draw,
    .handle_key = term_handle_key,
    .handle_mouse = 0,
};

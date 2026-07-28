#include <nyx/npx.h>
#include <nyx/apps/terminal.h>

const struct app_exports __attribute__((section(".npx_exports"), used)) npx_app = {
    .name = "Terminal",
    .init = term_init,
    .draw = term_draw,
    .handle_key = term_handle_key,
    .handle_mouse = 0,
};

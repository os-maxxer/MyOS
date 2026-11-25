#include <solis/spx.h>
#include <solis/apps/browser.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Browser",
    .init = browser_init,
    .draw = browser_draw,
    .handle_key = browser_handle_key,
    .handle_mouse = browser_handle_mouse,
    .handle_scroll = browser_handle_scroll,
    .get_size = browser_get_size,
    .open_file = browser_open_file,
    .tick = browser_tick,
};

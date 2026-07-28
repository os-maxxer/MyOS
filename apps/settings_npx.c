#include <nyx/npx.h>
#include <nyx/apps/settings.h>

const struct app_exports __attribute__((section(".npx_exports"), used)) npx_app = {
    .name = "Settings",
    .init = settings_init,
    .draw = settings_draw,
    .handle_key = settings_handle_key,
    .handle_mouse = settings_handle_mouse,
};

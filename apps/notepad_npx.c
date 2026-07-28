#include <nyx/npx.h>
#include <nyx/apps/notepad.h>

const struct app_exports __attribute__((section(".npx_exports"), used)) npx_app = {
    .name = "Notepad",
    .init = notepad_init,
    .draw = notepad_draw,
    .handle_key = notepad_handle_key,
    .handle_mouse = notepad_handle_mouse,
};

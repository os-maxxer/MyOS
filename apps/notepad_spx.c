#include <solis/spx.h>
#include <solis/apps/notepad.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Notepad",
    .init = notepad_init,
    .draw = notepad_draw,
    .handle_key = notepad_handle_key,
    .handle_mouse = notepad_handle_mouse,
};

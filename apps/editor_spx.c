#include <solis/spx.h>
#include <solis/apps/editor.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Editor",
    .init = editor_init,
    .draw = editor_draw,
    .handle_key = editor_handle_key,
    .handle_mouse = editor_handle_mouse,
    .open_file = editor_open_file,
};

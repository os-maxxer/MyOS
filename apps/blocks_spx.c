#include <solis/spx.h>
#include <solis/apps/blocks.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Blocks",
    .init = blocks_init,
    .draw = blocks_draw,
    .handle_key = blocks_handle_key,
    .handle_mouse = blocks_handle_mouse,
};
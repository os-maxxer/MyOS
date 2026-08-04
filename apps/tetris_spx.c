#include <solis/spx.h>
#include <solis/apps/tetris.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Tetris",
    .init = tetris_init,
    .draw = tetris_draw,
    .handle_key = tetris_handle_key,
    .handle_mouse = 0,
};

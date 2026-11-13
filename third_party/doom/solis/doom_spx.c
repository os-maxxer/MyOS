#include <solis/spx.h>

void doom_app_init(void);
void doom_app_draw(int x, int y, int w, int h);
void doom_app_handle_key(char key);
int doom_app_tick(void);
void doom_app_get_size(int *w, int *h);

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Doom",
    .init = doom_app_init,
    .draw = doom_app_draw,
    .handle_key = doom_app_handle_key,
    .handle_mouse = 0,
    .get_size = doom_app_get_size,
    .handle_scroll = 0,
    .handle_context = 0,
    .open_file = 0,
    .tick = doom_app_tick,
};

#include <solis/spx.h>
#include <solis/apps/calculator.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Calculator",
    .init = calculator_init,
    .draw = calculator_draw,
    .handle_key = calculator_handle_key,
    .handle_mouse = calculator_handle_mouse,
    .get_size = calculator_get_size,
};

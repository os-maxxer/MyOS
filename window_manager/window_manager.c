/*
 * Window manager facade for Milestone 3.
 */

#include <solis/window_manager.h>
#include <solis/gui.h>

void window_manager_init(void) {
    gui_init();
}

void window_manager_redraw(void) {
    gui_redraw();
}

void window_manager_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons) {
    gui_handle_mouse(dx, dy, buttons);
}

void window_manager_handle_key(char key) {
    gui_handle_key(key);
}

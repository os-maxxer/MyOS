/*
 * Desktop shell for Milestone 3.
 */

#include <myos/window_manager.h>

void desktop_init(void) {
    window_manager_init();
}

void desktop_redraw(void) {
    window_manager_redraw();
}

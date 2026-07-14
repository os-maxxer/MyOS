/*
 * A minimal notepad-like application for Milestone 3.
 */

#include <myos/graphics.h>
#include <myos/console.h>

void app_notepad_show(void) {
    graphics_fill_rect(80, 80, 300, 200, 0xFFFFFFFF);
    graphics_draw_rect(80, 80, 300, 200, 0xFF4A90E2);
    graphics_draw_string(92, 96, "Notepad", 0xFF222222);
    graphics_draw_string(92, 120, "This is a placeholder app.", 0xFF222222);
}

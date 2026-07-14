/*
 * Lightweight GUI shell for Milestone 3.
 * This implements a simple desktop with draggable windows and a taskbar.
 */

#include <myos/gui.h>
#include <myos/graphics.h>
#include <myos/console.h>
#include <myos/mouse.h>
#include <stdbool.h>

#define MAX_WINDOWS 8

struct gui_window {
    bool visible;
    bool focused;
    bool dragging;
    struct gui_rect bounds;
    char title[24];
};

static struct gui_window windows[MAX_WINDOWS];
static struct gui_point mouse_position = {100, 100};
static uint8_t mouse_buttons = 0;
static int active_window = -1;
static int mouse_window = -1;

static void gui_draw_taskbar(void) {
    graphics_fill_rect(0, 460, 640, 40, 0xFF3B3B3B);
    graphics_fill_rect(4, 468, 60, 24, 0xFF4A90E2);
    graphics_draw_string(18, 472, "Start", 0xFFFFFFFF);
    graphics_draw_string(520, 472, "12:00", 0xFFFFFFFF);
}

static void gui_draw_desktop(void) {
    graphics_clear(0xFF1B1B1B);
    graphics_fill_rect(10, 20, 120, 80, 0xFF4A90E2);
    graphics_draw_rect(10, 20, 120, 80, 0xFFFFFFFF);
    graphics_draw_string(24, 34, "MyOS", 0xFFFFFFFF);
    graphics_draw_string(24, 54, "Files", 0xFFFFFFFF);
    graphics_draw_string(24, 74, "Note", 0xFFFFFFFF);
}

static void gui_draw_window(struct gui_window *window) {
    if (!window->visible) {
        return;
    }
    uint32_t title_color = window->focused ? 0xFF4A90E2 : 0xFF7A7A7A;
    graphics_fill_rect(window->bounds.x, window->bounds.y, window->bounds.width, 24, title_color);
    graphics_draw_string(window->bounds.x + 8, window->bounds.y + 6, window->title, 0xFFFFFFFF);
    graphics_draw_rect(window->bounds.x, window->bounds.y, window->bounds.width, window->bounds.height, 0xFFB0B0B0);
    graphics_fill_rect(window->bounds.x + 2, window->bounds.y + 26, window->bounds.width - 4, window->bounds.height - 28, 0xFFFFFFFF);
}

static void gui_layout_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!windows[i].visible) {
            continue;
        }
        gui_draw_window(&windows[i]);
    }
}

void gui_init(void) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        windows[i].visible = false;
        windows[i].focused = false;
        windows[i].dragging = false;
        windows[i].bounds.x = 0;
        windows[i].bounds.y = 0;
        windows[i].bounds.width = 240;
        windows[i].bounds.height = 180;
        windows[i].title[0] = '\0';
    }

    windows[0].visible = true;
    windows[0].focused = true;
    windows[0].bounds.x = 80;
    windows[0].bounds.y = 60;
    windows[0].bounds.width = 280;
    windows[0].bounds.height = 180;
    for (int i = 0; i < 23; ++i) {
        windows[0].title[i] = '\0';
    }
    windows[0].title[0] = 'W';
    windows[0].title[1] = 'e';
    windows[0].title[2] = 'l';
    windows[0].title[3] = 'c';
    windows[0].title[4] = 'o';
    windows[0].title[5] = 'm';
    windows[0].title[6] = 'e';
    windows[0].title[7] = '\0';
}

void gui_redraw(void) {
    gui_draw_desktop();
    gui_layout_windows();
    gui_draw_taskbar();
    graphics_draw_mouse_cursor(mouse_position.x, mouse_position.y, 0xFFFFFFFF);
}

void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons) {
    mouse_position.x += dx;
    mouse_position.y += dy;
    mouse_buttons = buttons;

    if (mouse_buttons & 0x01) {
        for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
            if (!windows[i].visible) {
                continue;
            }
            if (mouse_position.x >= windows[i].bounds.x && mouse_position.x < windows[i].bounds.x + windows[i].bounds.width && mouse_position.y >= windows[i].bounds.y && mouse_position.y < windows[i].bounds.y + windows[i].bounds.height) {
                active_window = i;
                windows[i].focused = true;
                mouse_window = i;
                break;
            }
        }
    }

    for (int i = 0; i < MAX_WINDOWS; ++i) {
        windows[i].focused = (i == active_window);
    }
}

void gui_handle_key(char key) {
    (void)key;
}

#include <nyx/gui.h>
#include <nyx/graphics.h>
#include <nyx/console.h>
#include <nyx/mouse.h>
#include <nyx/keyboard.h>
#include <nyx/apps/notepad.h>
#include <nyx/apps/terminal.h>
#include <nyx/apps/paint.h>
#include <stdbool.h>

#define MAX_WINDOWS 8

struct gui_window {
    bool visible;
    bool focused;
    bool dragging;
    int drag_off_x;
    int drag_off_y;
    struct gui_rect bounds;
    char title[24];
    int app_id;
};

static struct gui_window windows[MAX_WINDOWS];
static int mouse_x = 400;
static int mouse_y = 300;
static uint8_t mouse_buttons = 0;
static int active_window = -1;
static int drag_window = -1;
static bool redraw_pending = true;
static bool start_menu_open = false;

#define START_BTN_X 4
#define START_BTN_Y (graphics_get_height() - 32)
#define START_BTN_W 60
#define START_BTN_H 24
#define START_MENU_X 4
#define START_MENU_Y (graphics_get_height() - 240)
#define START_MENU_W 160
#define START_MENU_H 200
#define MENU_ITEMS 3


static void gui_draw_start_menu(void) {
    int mx = START_MENU_X;
    int my = START_MENU_Y;
    graphics_fill_rect(mx, my, START_MENU_W, START_MENU_H, 0xFFCCCCCC);
    graphics_draw_rect(mx, my, START_MENU_W, START_MENU_H, 0xFF888888);

    const char *items[] = {"Terminal", "Notes", "Paint"};
    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = my + 8 + i * 40;
        graphics_fill_rect(mx + 8, iy, 144, 32, 0xFFEEEEEE);
        graphics_draw_rect(mx + 8, iy, 144, 32, 0xFFAAAAAA);
        graphics_draw_string(mx + 16, iy + 8, items[i], 0xFF222222);
    }
}

static void gui_draw_taskbar(void) {
    int bar_top = graphics_get_height() - 40;
    graphics_fill_rect(0, bar_top, graphics_get_width(), 40, 0xFF2D2D2D);
    graphics_fill_rect(0, bar_top, graphics_get_width(), 2, 0xFF6A9CF5);

    uint32_t start_color = start_menu_open ? 0xFF5A8AFF : 0xFF4A90E2;
    graphics_fill_rect(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, start_color);
    graphics_draw_rect(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, 0xFFFFFFFF);
    graphics_draw_string(18, graphics_get_height() - 28, "Start", 0xFFFFFFFF);
    graphics_draw_string(graphics_get_width() - 80, graphics_get_height() - 28, "12:00", 0xFFFFFFFF);

    if (start_menu_open) {
        gui_draw_start_menu();
    }
}

static void gui_draw_desktop(void) {
    graphics_clear(0xFFADD8E6);
    graphics_fill_rect(20, 40, 100, 80, 0xFF3B8B3B);
    graphics_draw_rect(20, 40, 100, 80, 0xFFFFFFFF);
    graphics_draw_string(36, 54, "Terminal", 0xFFFFFFFF);
    graphics_draw_string(36, 68, "  [_] ", 0xFFFFFFFF);

    graphics_fill_rect(140, 40, 100, 80, 0xFF4A90E2);
    graphics_draw_rect(140, 40, 100, 80, 0xFFFFFFFF);
    graphics_draw_string(158, 54, "Notes", 0xFFFFFFFF);
    graphics_draw_string(158, 68, "  ___ ", 0xFFFFFFFF);

    graphics_fill_rect(260, 40, 100, 80, 0xFFE24A4A);
    graphics_draw_rect(260, 40, 100, 80, 0xFFFFFFFF);
    graphics_draw_string(280, 54, "Paint", 0xFFFFFFFF);
    graphics_draw_string(280, 68, "  /_\\ ", 0xFFFFFFFF);
}

static void gui_draw_window(struct gui_window *window) {
    if (!window->visible) return;
    uint32_t title_color = window->focused ? 0xFF4A90E2 : 0xFF7A7A7A;
    graphics_fill_rect(window->bounds.x, window->bounds.y,
                       window->bounds.width, 24, title_color);
    graphics_draw_string(window->bounds.x + 8, window->bounds.y + 6,
                         window->title, 0xFFFFFFFF);

    int close_x = window->bounds.x + window->bounds.width - 24;
    int close_y = window->bounds.y + 2;
    graphics_fill_rect(close_x, close_y, 20, 20, 0xFFCC3333);
    graphics_draw_string(close_x + 6, close_y + 3, "X", 0xFFFFFFFF);

    graphics_draw_rect(window->bounds.x, window->bounds.y,
                       window->bounds.width, window->bounds.height, 0xFFB0B0B0);
}

static void gui_layout_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!windows[i].visible) continue;
        gui_draw_window(&windows[i]);
        int cx = windows[i].bounds.x + 2;
        int cy = windows[i].bounds.y + 26;
        int cw = windows[i].bounds.width - 4;
        int ch = windows[i].bounds.height - 28;

        if (windows[i].app_id == 1) {
            graphics_fill_rect(cx, cy, cw, ch, 0xFF000000);
            term_draw(cx, cy, cw, ch);
        } else if (windows[i].app_id == 2) {
            graphics_fill_rect(cx, cy, cw, ch, 0xFFFFFFFF);
            notepad_draw(cx, cy, cw, ch);
        } else if (windows[i].app_id == 3) {
            graphics_fill_rect(cx, cy, cw, ch, 0xFFFFFFFF);
            paint_draw(cx, cy, cw, ch);
        } else {
            graphics_fill_rect(cx, cy, cw, ch, 0xFFFFFFF0);
        }
    }
}

void gui_init(void) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        windows[i].visible = false;
        windows[i].focused = false;
        windows[i].dragging = false;
        windows[i].bounds.x = 0;
        windows[i].bounds.y = 0;
        windows[i].bounds.width = 480;
        windows[i].bounds.height = 400;
        windows[i].title[0] = '\0';
        windows[i].app_id = 0;
    }
    start_menu_open = false;
    redraw_pending = true;
}

static int gui_alloc_window(const char *title, int app_id) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!windows[i].visible) {
            windows[i].visible = true;
            windows[i].focused = true;
            windows[i].dragging = false;
            windows[i].bounds.x = 60 + (app_id * 30);
            windows[i].bounds.y = 80 + (app_id * 30);
            windows[i].bounds.width = 500;
            windows[i].bounds.height = 380;
            windows[i].app_id = app_id;
            int j = 0;
            while (title[j] && j < 23) {
                windows[i].title[j] = title[j];
                j++;
            }
            windows[i].title[j] = '\0';
            active_window = i;
            for (int k = 0; k < MAX_WINDOWS; ++k)
                windows[k].focused = (k == i);
            redraw_pending = true;
            return i;
        }
    }
    return -1;
}

int gui_launch_terminal(void) {
    return gui_alloc_window("Terminal", 1);
}

int gui_launch_notes(void) {
    return gui_alloc_window("Notes", 2);
}

int gui_launch_paint(void) {
    return gui_alloc_window("Paint", 3);
}

void gui_redraw(void) {
    gui_draw_desktop();
    gui_layout_windows();
    gui_draw_taskbar();
    graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
    redraw_pending = false;
}

void gui_toggle_start_menu(void) {
    start_menu_open = !start_menu_open;
    redraw_pending = true;
}

void gui_close_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    windows[idx].visible = false;
    windows[idx].focused = false;
    active_window = -1;
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        if (windows[i].visible) {
            active_window = i;
            windows[i].focused = true;
            break;
        }
    }
    redraw_pending = true;
}

static bool gui_point_in_rect(int px, int py, struct gui_rect *r) {
    return px >= r->x && px < r->x + r->width &&
           py >= r->y && py < r->y + r->height;
}

void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons) {
    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
    if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;

    bool left_down = buttons & 0x01;
    static bool prev_left = false;
    bool left_click = left_down && !prev_left;
    bool left_release = !left_down && prev_left;
    prev_left = left_down;

    if (left_down && drag_window >= 0) {
        int new_x = mouse_x - windows[drag_window].drag_off_x;
        int new_y = mouse_y - windows[drag_window].drag_off_y;
        if (new_x != windows[drag_window].bounds.x ||
            new_y != windows[drag_window].bounds.y) {
            windows[drag_window].bounds.x = new_x;
            windows[drag_window].bounds.y = new_y;
            redraw_pending = true;
        }
        mouse_buttons = buttons;
        return;
    }

    if (left_release && drag_window >= 0) {
        windows[drag_window].dragging = false;
        drag_window = -1;
        redraw_pending = true;
        mouse_buttons = buttons;
        return;
    }

    if (left_click) {
        if (start_menu_open) {
            int mx = START_MENU_X;
            int my = START_MENU_Y;
            if (mouse_x >= mx && mouse_x < mx + START_MENU_W &&
                mouse_y >= my && mouse_y < my + START_MENU_H) {
                for (int i = 0; i < MENU_ITEMS; i++) {
                    int iy = my + 8 + i * 40;
                    if (mouse_y >= iy && mouse_y < iy + 32 &&
                        mouse_x >= mx + 8 && mouse_x < mx + 8 + 144) {
                        if (i == 0) gui_launch_terminal();
                        else if (i == 1) gui_launch_notes();
                        else if (i == 2) gui_launch_paint();
                        start_menu_open = false;
                        mouse_buttons = buttons;
                        return;
                    }
                }
            }
            start_menu_open = false;
            redraw_pending = true;
        }

        bool hit_window = false;
        for (int i = MAX_WINDOWS - 1; i >= 0; --i) {
            if (!windows[i].visible) continue;
            struct gui_rect r = windows[i].bounds;
            if (gui_point_in_rect(mouse_x, mouse_y, &r)) {
                struct gui_rect close_btn = {
                    r.x + r.width - 24, r.y + 2, 20, 20
                };
                if (gui_point_in_rect(mouse_x, mouse_y, &close_btn)) {
                    gui_close_window(i);
                    mouse_buttons = buttons;
                    return;
                }
                active_window = i;
                for (int k = 0; k < MAX_WINDOWS; ++k)
                    windows[k].focused = (k == i);
                if (mouse_y < r.y + 24) {
                    windows[i].dragging = true;
                    drag_window = i;
                    windows[i].drag_off_x = mouse_x - r.x;
                    windows[i].drag_off_y = mouse_y - r.y;
                }
                hit_window = true;
                redraw_pending = true;
                break;
            }
        }

        if (!hit_window) {
            if (gui_point_in_rect(mouse_x, mouse_y,
                &(struct gui_rect){START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H})) {
                gui_toggle_start_menu();
            } else if (gui_point_in_rect(mouse_x, mouse_y,
                &(struct gui_rect){20, 40, 100, 80})) {
                gui_launch_terminal();
            } else if (gui_point_in_rect(mouse_x, mouse_y,
                &(struct gui_rect){140, 40, 100, 80})) {
                gui_launch_notes();
            } else if (gui_point_in_rect(mouse_x, mouse_y,
                &(struct gui_rect){260, 40, 100, 80})) {
                gui_launch_paint();
            }
        }
    }

    if (dx || dy) redraw_pending = true;
    mouse_buttons = buttons;
}

void gui_handle_key(char key) {
    if (active_window < 0 || active_window >= MAX_WINDOWS) return;
    int app = windows[active_window].app_id;
    if (app == 1) term_handle_key(key);
    else if (app == 2) notepad_handle_key(key);
    else if (app == 3) paint_handle_key(key);
    redraw_pending = true;
}

bool gui_needs_redraw(void) {
    return redraw_pending;
}

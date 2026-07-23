#include <nyx/gui.h>
#include <nyx/graphics.h>
#include <nyx/console.h>
#include <nyx/mouse.h>
#include <nyx/keyboard.h>
#include <nyx/apps/notepad.h>
#include <nyx/apps/terminal.h>
#include <nyx/apps/paint.h>
#include <nyx/apps/settings.h>
#include <nyx/apps/filebrowser.h>
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
static int gui_theme = GUI_THEME_SOLID;
static uint32_t bg_color = 0xFFADD8E6;

#define CURSOR_SIZE 16
static uint32_t cursor_bg[CURSOR_SIZE * CURSOR_SIZE];
static int prev_cursor_x = -1;
static int prev_cursor_y = -1;

static void cursor_save_bg(int x, int y) {
    for (int r = 0; r < CURSOR_SIZE; r++)
        for (int c = 0; c < CURSOR_SIZE; c++)
            cursor_bg[r * CURSOR_SIZE + c] = graphics_get_pixel(x + c, y + r);
}

static void cursor_restore_bg(int x, int y) {
    for (int r = 0; r < CURSOR_SIZE; r++)
        for (int c = 0; c < CURSOR_SIZE; c++)
            graphics_put_pixel(x + c, y + r, cursor_bg[r * CURSOR_SIZE + c]);
}

#define START_BTN_X 4
#define START_BTN_Y (graphics_get_height() - 32)
#define START_BTN_W 60
#define START_BTN_H 24
#define START_MENU_X 4
#define START_MENU_Y (graphics_get_height() - 288)
#define START_MENU_W 160
#define START_MENU_H 248
#define MENU_ITEMS 5


static void gui_draw_start_menu(void) {
    int mx = START_MENU_X;
    int my = START_MENU_Y;
    graphics_fill_rect(mx, my, START_MENU_W, START_MENU_H, 0xFFCCCCCC);
    graphics_draw_rect(mx, my, START_MENU_W, START_MENU_H, 0xFF888888);

    const char *items[] = {"Terminal", "Notes", "Paint", "Settings", "Files"};
    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = my + 8 + i * 40;
        graphics_fill_rect(mx + 8, iy, 144, 32, 0xFFEEEEEE);
        graphics_draw_rect(mx + 8, iy, 144, 32, 0xFFAAAAAA);
        graphics_draw_string(mx + 16, iy + 8, items[i], 0xFF222222);
    }
}

static uint32_t lerp_color(uint32_t c1, uint32_t c2, int t, int max) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + ((r2 - r1) * t / max);
    uint8_t g = g1 + ((g2 - g1) * t / max);
    uint8_t b = b1 + ((b2 - b1) * t / max);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void draw_starfield_bg(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    uint32_t c1 = 0xFF0D0D2B, c2 = 0xFF1A0A3E;
    for (uint32_t y = 0; y < h; y++)
        graphics_fill_rect(0, y, w, 1, lerp_color(c1, c2, y, h));

    int cx = (int)w / 3, cy = (int)h / 4;
    for (int r = 80; r > 0; r -= 2)
        graphics_fill_rect(cx - r, cy - r, r * 2, r * 2, lerp_color(0x00000000, 0x223366FF, r, 80));
    cx = (int)w * 2 / 3; cy = (int)h * 2 / 3;
    for (int r = 60; r > 0; r -= 2)
        graphics_fill_rect(cx - r, cy - r, r * 2, r * 2, lerp_color(0x00000000, 0x226633AA, r, 60));

    uint32_t seed = 42;
    for (int i = 0; i < 120; i++) {
        seed = seed * 1103515245 + 12345;
        uint32_t sx = (seed >> 16) % w;
        seed = seed * 1103515245 + 12345;
        uint32_t sy = (seed >> 16) % h;
        seed = seed * 1103515245 + 12345;
        uint32_t b = ((seed >> 16) & 0x7F) + 0x80;
        graphics_put_pixel(sx, sy, 0xFF000000 | (b << 16) | (b << 8) | b);
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
    if (gui_theme == GUI_THEME_STARFIELD) {
        draw_starfield_bg();
    } else {
        graphics_clear(bg_color);
    }
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

    graphics_fill_rect(380, 40, 100, 80, 0xFF8B5CF6);
    graphics_draw_rect(380, 40, 100, 80, 0xFFFFFFFF);
    graphics_draw_string(396, 54, "Settings", 0xFFFFFFFF);
    graphics_draw_string(396, 68, "  [_] ", 0xFFFFFFFF);

    graphics_fill_rect(500, 40, 100, 80, 0xFFD4A050);
    graphics_draw_rect(500, 40, 100, 80, 0xFFFFFFFF);
    graphics_draw_string(520, 54, "Files", 0xFFFFFFFF);
    graphics_draw_string(520, 68, "  |>  ", 0xFFFFFFFF);
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
        } else if (windows[i].app_id == 4) {
            settings_draw(cx, cy, cw, ch);
        } else if (windows[i].app_id == 5) {
            filebrowser_draw(cx, cy, cw, ch);
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

int gui_launch_settings(void) {
    return gui_alloc_window("Settings", 4);
}

int gui_launch_filebrowser(void) {
    return gui_alloc_window("Files", 5);
}

void gui_set_bg_color(uint32_t color) {
    bg_color = color;
    gui_theme = GUI_THEME_SOLID;
    redraw_pending = true;
}

uint32_t gui_get_bg_color(void) {
    return bg_color;
}

void gui_set_theme(int theme) {
    gui_theme = theme;
    redraw_pending = true;
}

int gui_get_theme(void) {
    return gui_theme;
}

void gui_redraw(void) {
    gui_draw_desktop();
    gui_layout_windows();
    gui_draw_taskbar();
    cursor_save_bg(mouse_x, mouse_y);
    graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
    prev_cursor_x = mouse_x;
    prev_cursor_y = mouse_y;
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
    bool left_down = buttons & 0x01;
    static bool prev_left = false;
    bool left_click = left_down && !prev_left;
    bool left_release = !left_down && prev_left;
    prev_left = left_down;

    if ((dx || dy) && !left_down && drag_window < 0) {
        if (prev_cursor_x >= 0)
            cursor_restore_bg(prev_cursor_x, prev_cursor_y);
        mouse_x += dx;
        mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
        if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;
        cursor_save_bg(mouse_x, mouse_y);
        graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
        prev_cursor_x = mouse_x;
        prev_cursor_y = mouse_y;
        mouse_buttons = buttons;
        redraw_pending = true;
        return;
    }

    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
    if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;

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
                        else if (i == 3) gui_launch_settings();
                        else if (i == 4) gui_launch_filebrowser();
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
                } else if (windows[i].app_id == 2) {
                    int cx = r.x + 2;
                    int cy = r.y + 26;
                    int cw = r.width - 4;
                    int ch = r.height - 28;
                    notepad_handle_mouse(cx, cy, cw, ch, mouse_x, mouse_y);
                } else if (windows[i].app_id == 4) {
                    int cx = r.x + 2;
                    int cy = r.y + 26;
                    int cw = r.width - 4;
                    int ch = r.height - 28;
                    settings_handle_mouse(cx, cy, cw, ch, mouse_x, mouse_y);
                } else if (windows[i].app_id == 5) {
                    int cx = r.x + 2;
                    int cy = r.y + 26;
                    int cw = r.width - 4;
                    int ch = r.height - 28;
                    filebrowser_handle_mouse(cx, cy, cw, ch, mouse_x, mouse_y);
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
            } else if (gui_point_in_rect(mouse_x, mouse_y,
                &(struct gui_rect){380, 40, 100, 80})) {
                gui_launch_settings();
            } else if (gui_point_in_rect(mouse_x, mouse_y,
                &(struct gui_rect){500, 40, 100, 80})) {
                gui_launch_filebrowser();
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
    else if (app == 4) settings_handle_key(key);
    else if (app == 5) filebrowser_handle_key(key);
    redraw_pending = true;
}

bool gui_needs_redraw(void) {
    return redraw_pending;
}

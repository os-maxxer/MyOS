#include <nyx/gui.h>
#include <nyx/graphics.h>
#include <nyx/console.h>
#include <nyx/mouse.h>
#include <nyx/keyboard.h>
#include <nyx/bmp.h>
#include <nyx/npx.h>
#include <stdbool.h>

#define MAX_WINDOWS 8
#define TASKBAR_H 32
#define TASKBAR_Y (graphics_get_height() - TASKBAR_H)
#define START_BTN_W 52
#define START_BTN_X 4
#define START_BTN_Y (TASKBAR_Y + 4)
#define START_BTN_H (TASKBAR_H - 8)
#define TRAY_W 80
#define MINIMAP_SIZE 80

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
static uint32_t bg_color = 0xFF2E3436;

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

#define START_MENU_X 4
#define START_MENU_Y (TASKBAR_Y - 248)
#define START_MENU_W 160
#define START_MENU_H 248
#define MENU_ITEMS 5

static void gui_draw_start_menu(void) {
    int mx = START_MENU_X;
    int my = START_MENU_Y;
    graphics_fill_rect(mx, my, START_MENU_W, START_MENU_H, 0xFFE8E8E8);
    graphics_draw_rect(mx, my, START_MENU_W, START_MENU_H, 0xFF888888);
    graphics_fill_rect(mx, my, START_MENU_W, 24, 0xFF4A90E2);
    graphics_draw_string(mx + 8, my + 5, "IceWM Menu", 0xFFFFFFFF);

    const char *items[] = {"Terminal", "Notes", "Paint", "Settings", "Files"};
    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = my + 28 + i * 40;
        uint32_t bg = (i % 2 == 0) ? 0xFFF0F0F0 : 0xFFE0E0E0;
        graphics_fill_rect(mx + 4, iy, START_MENU_W - 8, 36, bg);
        graphics_draw_string(mx + 16, iy + 10, items[i], 0xFF222222);
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
    int bw = graphics_get_width();
    graphics_fill_rect(0, TASKBAR_Y, bw, TASKBAR_H, 0xFF2E3436);
    graphics_fill_rect(0, TASKBAR_Y, bw, 1, 0xFF888888);

    uint32_t start_color = start_menu_open ? 0xFF3A7BD5 : 0xFF4A90E2;
    graphics_fill_rect(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, start_color);
    graphics_draw_rect(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, 0xFF6A9CF5);
    graphics_draw_string(START_BTN_X + 6, START_BTN_Y + 5, "Ice", 0xFFFFFFFF);

    int task_x = START_BTN_X + START_BTN_W + 4;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) continue;
        int tw = 100;
        if (task_x + tw > bw - TRAY_W - 8) tw = bw - TRAY_W - 8 - task_x;
        if (tw < 30) break;
        uint32_t c = windows[i].focused ? 0xFF5A5A5A : 0xFF3A3A3A;
        graphics_fill_rect(task_x, START_BTN_Y, tw, START_BTN_H, c);
        graphics_draw_rect(task_x, START_BTN_Y, tw, START_BTN_H, 0xFF555555);
        if (tw > 20) graphics_draw_string(task_x + 4, START_BTN_Y + 5, windows[i].title, 0xFFFFFFFF);
        task_x += tw + 2;
    }

    graphics_draw_string(bw - TRAY_W + 8, START_BTN_Y + 5, "12:00", 0xFFAAAAAA);

    if (start_menu_open) gui_draw_start_menu();
}

static void gui_draw_desktop(void) {
    if (bmp_is_wallpaper_loaded()) {
        bmp_blit_wallpaper();
    } else if (gui_theme == GUI_THEME_STARFIELD) {
        draw_starfield_bg();
    } else {
        graphics_fill_gradient_v(0, 0, graphics_get_width(), graphics_get_height(), 0xFF2D0A3E, 0xFF5E2C1E);
    }

    int icons[][4] = {{20, 40, 80, 72}, {120, 40, 80, 72}, {220, 40, 80, 72}, {320, 40, 80, 72}, {420, 40, 80, 72}};
    const char *labels[] = {"Terminal", "Notes", "Paint", "Settings", "Files"};
    uint32_t colors[] = {0xFF3B8B3B, 0xFF4A90E2, 0xFFE24A4A, 0xFF8B5CF6, 0xFFD4A050};
    for (int i = 0; i < 5; i++) {
        graphics_fill_rect(icons[i][0], icons[i][1], icons[i][2], icons[i][3], colors[i]);
        graphics_draw_rect(icons[i][0], icons[i][1], icons[i][2], icons[i][3], 0xAAFFFFFF);
        graphics_draw_string(icons[i][0] + 20, icons[i][1] + 28, labels[i], 0xFFFFFFFF);
    }
}

static void gui_draw_window(struct gui_window *window) {
    if (!window->visible) return;
    struct gui_rect r = window->bounds;
    uint32_t title_top = window->focused ? 0xFF4A90E2 : 0xFF6A6A6A;
    uint32_t title_bot = window->focused ? 0xFF2A70C2 : 0xFF5A5A5A;
    graphics_fill_gradient_v(r.x, r.y, r.width, 24, title_top, title_bot);
    graphics_draw_string(r.x + 6, r.y + 5, window->title, 0xFFFFFFFF);

    int btn_x = r.x + r.width - 22;
    graphics_fill_rect(btn_x, r.y + 3, 18, 18, 0xFFCC3333);
    graphics_draw_rect(btn_x, r.y + 3, 18, 18, 0xFFAA2222);
    graphics_draw_string(btn_x + 5, r.y + 4, "X", 0xFFFFFFFF);

    btn_x -= 22;
    graphics_fill_rect(btn_x, r.y + 3, 18, 18, 0xFFCCAA33);
    graphics_draw_rect(btn_x, r.y + 3, 18, 18, 0xFFAA8822);
    graphics_draw_string(btn_x + 5, r.y + 4, "[]", 0xFFFFFFFF);

    graphics_fill_rect(r.x, r.y + 24, r.width, r.height - 24, 0xFF444444);
    graphics_draw_rect(r.x, r.y, r.width, r.height, 0xFF888888);
    graphics_draw_rect(r.x + 1, r.y + 1, r.width - 2, r.height - 2, 0xFF555555);
}

static void gui_layout_windows(void) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!windows[i].visible) continue;
        gui_draw_window(&windows[i]);
        int cx = windows[i].bounds.x + 2;
        int cy = windows[i].bounds.y + 26;
        int cw = windows[i].bounds.width - 4;
        int ch = windows[i].bounds.height - 28;
        const struct app_exports *app = npx_get_exports(windows[i].app_id - 1);
        if (app && app->draw) {
            app->draw(cx, cy, cw, ch);
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
    bmp_cache_wallpaper("/desktop/wallpaper.bmp");
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

int gui_launch_terminal(void) { return gui_alloc_window("Terminal", 1); }
int gui_launch_notes(void) { return gui_alloc_window("Notes", 2); }
int gui_launch_paint(void) { return gui_alloc_window("Paint", 3); }
int gui_launch_settings(void) { return gui_alloc_window("Settings", 4); }
int gui_launch_filebrowser(void) { return gui_alloc_window("Files", 5); }

void gui_set_bg_color(uint32_t color) { bg_color = color; gui_theme = GUI_THEME_SOLID; redraw_pending = true; }
uint32_t gui_get_bg_color(void) { return bg_color; }
void gui_set_theme(int theme) { gui_theme = theme; redraw_pending = true; }
int gui_get_theme(void) { return gui_theme; }

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
    return px >= r->x && px < r->x + r->width && py >= r->y && py < r->y + r->height;
}

void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons) {
    bool left_down = buttons & 0x01;
    static bool prev_left = false;
    bool left_click = left_down && !prev_left;
    bool left_release = !left_down && prev_left;
    prev_left = left_down;

    if ((dx || dy) && !left_down && drag_window < 0) {
        if (prev_cursor_x >= 0) cursor_restore_bg(prev_cursor_x, prev_cursor_y);
        mouse_x += dx; mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
        if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;
        cursor_save_bg(mouse_x, mouse_y);
        graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
        prev_cursor_x = mouse_x;
        prev_cursor_y = mouse_y;
        mouse_buttons = buttons;
        return;
    }

    mouse_x += dx; mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
    if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;

    if (left_down && drag_window >= 0) {
        int new_x = mouse_x - windows[drag_window].drag_off_x;
        int new_y = mouse_y - windows[drag_window].drag_off_y;
        if (new_x != windows[drag_window].bounds.x || new_y != windows[drag_window].bounds.y) {
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
            int mx = START_MENU_X, my = START_MENU_Y;
            if (mouse_x >= mx && mouse_x < mx + START_MENU_W && mouse_y >= my && mouse_y < my + START_MENU_H) {
                for (int i = 0; i < MENU_ITEMS; i++) {
                    int iy = my + 28 + i * 40;
                    if (mouse_y >= iy && mouse_y < iy + 36 && mouse_x >= mx + 4 && mouse_x < mx + START_MENU_W - 4) {
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
                struct gui_rect close_btn = {r.x + r.width - 22, r.y + 3, 18, 18};
                if (gui_point_in_rect(mouse_x, mouse_y, &close_btn)) {
                    gui_close_window(i);
                    mouse_buttons = buttons;
                    return;
                }
                active_window = i;
                for (int k = 0; k < MAX_WINDOWS; ++k) windows[k].focused = (k == i);
                if (mouse_y < r.y + 24) {
                    windows[i].dragging = true;
                    drag_window = i;
                    windows[i].drag_off_x = mouse_x - r.x;
                    windows[i].drag_off_y = mouse_y - r.y;
                } else {
                    const struct app_exports *app = npx_get_exports(windows[i].app_id - 1);
                    if (app && app->handle_mouse) {
                        int cx = r.x + 2, cy = r.y + 26, cw = r.width - 4, ch = r.height - 28;
                        app->handle_mouse(cx, cy, cw, ch, mouse_x, mouse_y);
                    }
                }
                hit_window = true;
                redraw_pending = true;
                break;
            }
        }

        if (!hit_window) {
            if (gui_point_in_rect(mouse_x, mouse_y, &(struct gui_rect){START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H})) {
                gui_toggle_start_menu();
            } else {
                int icons[][4] = {{20, 40, 80, 72}, {120, 40, 80, 72}, {220, 40, 80, 72}, {320, 40, 80, 72}, {420, 40, 80, 72}};
                for (int i = 0; i < 5; i++) {
                    struct gui_rect ir = {icons[i][0], icons[i][1], icons[i][2], icons[i][3]};
                    if (gui_point_in_rect(mouse_x, mouse_y, &ir)) {
                        if (i == 0) gui_launch_terminal();
                        else if (i == 1) gui_launch_notes();
                        else if (i == 2) gui_launch_paint();
                        else if (i == 3) gui_launch_settings();
                        else if (i == 4) gui_launch_filebrowser();
                        break;
                    }
                }
            }
        }
    }

    if (dx || dy) redraw_pending = true;
    mouse_buttons = buttons;
}

void gui_handle_key(char key) {
    if (active_window < 0 || active_window >= MAX_WINDOWS) return;
    int slot = windows[active_window].app_id - 1;
    const struct app_exports *app = npx_get_exports(slot);
    if (app && app->handle_key) app->handle_key(key);
    redraw_pending = true;
}

bool gui_needs_redraw(void) { return redraw_pending; }

int gui_launch_taskmanager(void) { return gui_alloc_window("Task Manager", 6); }
int gui_launch_pkg(void) { return gui_alloc_window("NYPKG", 7); }

int gui_launch_app(int slot) {
    switch (slot) {
        case 0: return gui_launch_terminal();
        case 1: return gui_launch_notes();
        case 2: return gui_launch_paint();
        case 3: return gui_launch_settings();
        case 4: return gui_launch_filebrowser();
        case 5: return gui_launch_taskmanager();
        case 6: return gui_launch_pkg();
        default: return -1;
    }
}

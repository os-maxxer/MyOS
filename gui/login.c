#include <solis/login.h>
#include <solis/graphics.h>
#include <solis/mouse.h>
#include <solis/keyboard.h>

#define PANEL_W 380
#define PANEL_H 220
#define BTN_W 200
#define BTN_H 44

#define C_BG_TOP    0xFF707CAF
#define C_BG_BOT    0xFF5C699F
#define C_PANEL     0xFF171E4B
#define C_PANEL_LN  0xFF46538E
#define C_TEXT      0xFFFFFFFF
#define C_TEXT_DIM  0xFFB8C2E6
#define C_ACTIVE    0xFF596DE8
#define C_ACTIVE_DK 0xFF394BB8

static int mouse_x = 512;
static int mouse_y = 384;
static int btn_rect[4];

static void fill_triangle(int x1, int y1, int x2, int y2,
                          int x3, int y3, uint32_t color) {
    int min_y = y1 < y2 ? y1 : y2;
    if (y3 < min_y) min_y = y3;
    int max_y = y1 > y2 ? y1 : y2;
    if (y3 > max_y) max_y = y3;
    for (int y = min_y; y < max_y; y++) {
        int xs[3], count = 0;
        int ax[3] = {x1, x2, x3}, ay[3] = {y1, y2, y3};
        int bx[3] = {x2, x3, x1}, by[3] = {y2, y3, y1};
        for (int i = 0; i < 3; i++) {
            if ((y >= ay[i] && y < by[i]) || (y >= by[i] && y < ay[i]))
                xs[count++] = ax[i] + (y - ay[i]) * (bx[i] - ax[i]) / (by[i] - ay[i]);
        }
        if (count == 2) {
            int left = xs[0] < xs[1] ? xs[0] : xs[1];
            int right = xs[0] > xs[1] ? xs[0] : xs[1];
            graphics_fill_rect(left, y, right - left + 1, 1, color);
        }
    }
}

static void draw_solis_mark(int cx, int cy, int size, uint32_t color) {
    int stroke = size / 8;
    int left = cx - size / 3;
    int top = cy - size / 2;
    int middle = cy - stroke / 2;
    int right = cx + size / 3 - stroke;
    graphics_fill_rect(left, top, size * 2 / 3, stroke, color);
    graphics_fill_rect(left, top, stroke, size / 2 + stroke, color);
    graphics_fill_rect(left, middle, size * 2 / 3, stroke, color);
    graphics_fill_rect(right, middle, stroke, size / 2, color);
    graphics_fill_rect(left, cy + size / 2 - stroke, size * 2 / 3, stroke, color);
}

static void draw_background(void) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    graphics_fill_gradient_v(0, 0, (uint32_t)w, (uint32_t)h, C_BG_TOP, C_BG_BOT);
    fill_triangle(0, 0, w * 14 / 100, 0, w * 14 / 100, h * 17 / 100, 0xFF202D80);
    fill_triangle(0, 0, w * 14 / 100, h * 17 / 100, 0, h * 14 / 100, 0xFF35438F);
    fill_triangle(w * 68 / 100, 0, w, 0, w, h * 40 / 100, 0xFF253381);
    fill_triangle(w * 74 / 100, 0, w, h * 10 / 100, w, h * 40 / 100, 0xFF32428F);
    fill_triangle(0, h * 23 / 100, w * 30 / 100, h * 69 / 100, 0, h * 92 / 100, 0xFF59679F);
    fill_triangle(0, h * 92 / 100, w * 30 / 100, h * 69 / 100, w * 14 / 100, h, 0xFF1D2A78);
    fill_triangle(w * 69 / 100, h * 82 / 100, w, h * 54 / 100, w * 91 / 100, h, 0xFF6573A7);
    fill_triangle(w * 69 / 100, h * 82 / 100, w * 91 / 100, h, w * 64 / 100, h, 0xFF202E7A);
}

static void draw_login_scene(bool hovered) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    int cx = w / 2;
    int logo_y = h / 4;
    int px = (w - PANEL_W) / 2;
    int py = (h - PANEL_H) / 2 + 48;

    draw_background();
    draw_solis_mark(cx + 4, logo_y + 5, 58, 0xFF101744);
    draw_solis_mark(cx, logo_y, 58, C_TEXT);
    graphics_draw_string(cx - 32, logo_y + 39, "SOLIS OS", C_TEXT);
    graphics_draw_string(cx - 76, logo_y + 62, "A calmer place to compute", C_TEXT_DIM);

    graphics_fill_rect(px + 5, py + 7, PANEL_W, PANEL_H, 0xFF303A78);
    graphics_fill_rect(px, py, PANEL_W, PANEL_H, C_PANEL);
    graphics_draw_rect(px, py, PANEL_W, PANEL_H, C_PANEL_LN);
    graphics_fill_rect(px + 1, py + 1, PANEL_W - 2, 2, 0xFF596DE8);
    graphics_draw_string(cx - 52, py + 30, "Welcome back", C_TEXT);
    graphics_draw_string(cx - 16, py + 72, "User", C_TEXT);
    graphics_draw_string(cx - 56, py + 96, "Local session ready", C_TEXT_DIM);

    int bx = cx - BTN_W / 2;
    int by = py + 144;
    btn_rect[0] = bx;
    btn_rect[1] = by;
    btn_rect[2] = BTN_W;
    btn_rect[3] = BTN_H;
    uint32_t button = hovered ? C_ACTIVE_DK : C_ACTIVE;
    graphics_fill_rounded_rect(bx, by, BTN_W, BTN_H, 7, button);
    graphics_draw_rounded_rect(bx, by, BTN_W, BTN_H, 7, 0xFF8290FF);
    graphics_draw_string(bx + (BTN_W - 8 * 8) / 2, by + 14, "Continue", C_TEXT);
}

static void redraw_login(bool hovered) {
    graphics_reset_clip();
    graphics_begin_frame();
    draw_login_scene(hovered);
    graphics_draw_mouse_cursor((uint32_t)mouse_x, (uint32_t)mouse_y, C_TEXT);
    graphics_end_frame();
}

void login_screen(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    if (mouse_x >= (int)w) mouse_x = (int)w - 1;
    if (mouse_y >= (int)h) mouse_y = (int)h - 1;

    bool hovered = false;
    redraw_login(hovered);

    bool done = false;
    while (!done) {
        struct mouse_state ms;
        mouse_get_state(&ms);
        if (ms.dx || ms.dy) {
            mouse_x += ms.dx;
            mouse_y += ms.dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)w) mouse_x = (int)w - 1;
            if (mouse_y >= (int)h) mouse_y = (int)h - 1;

            int bx = btn_rect[0], by = btn_rect[1];
            hovered = mouse_x >= bx && mouse_x < bx + BTN_W &&
                      mouse_y >= by && mouse_y < by + BTN_H;
            redraw_login(hovered);
        }

        if (ms.buttons & 0x01) {
            int bx = btn_rect[0], by = btn_rect[1];
            if (mouse_x >= bx && mouse_x < bx + BTN_W &&
                mouse_y >= by && mouse_y < by + BTN_H)
                done = true;
        }
        if (keyboard_has_input()) {
            char key;
            if (keyboard_read_char(&key)) done = true;
        }
        __asm__ volatile("hlt");
    }
}

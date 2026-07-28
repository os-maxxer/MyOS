#include <nyx/login.h>
#include <nyx/graphics.h>
#include <nyx/mouse.h>
#include <nyx/keyboard.h>

#define PANEL_W 380
#define PANEL_H 300
#define BTN_W 200
#define BTN_H 44
#define PANEL_R 12
#define BTN_R 8

static int mouse_x = 512;
static int mouse_y = 384;
static int prev_mx = -1;
static int prev_my = -1;
static uint32_t cursor_bg[256];
static int btn_rect[4];

static void save_cursor_bg(int x, int y) {
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            cursor_bg[r * 16 + c] = graphics_get_pixel(x + c, y + r);
}

static void restore_cursor_bg(int x, int y) {
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            graphics_put_pixel(x + c, y + r, cursor_bg[r * 16 + c]);
}

static void draw_logo(void) {
    uint32_t w = graphics_get_width();
    uint32_t cx = (int)w / 2;
    int logo_y = 80;
    graphics_fill_circle(cx, logo_y, 36, 0xFF6C63FF);
    graphics_fill_circle(cx, logo_y, 28, 0xFF1A1A3E);
    graphics_draw_string(cx - 24, logo_y - 46, "Nyx", 0xFF6C63FF);
    graphics_draw_string(cx - 28, logo_y + 52, "Nyx OS", 0xFFB0B0FF);
}

static void draw_panel(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    int px = ((int)w - PANEL_W) / 2;
    int py = ((int)h - PANEL_H) / 2 + 40;

    graphics_fill_rounded_rect(px - 2, py - 2, PANEL_W + 4, PANEL_H + 4, PANEL_R + 2, 0x22000000);
    graphics_fill_rounded_rect(px, py, PANEL_W, PANEL_H, PANEL_R, 0xDD1A1A2E);
    graphics_draw_rounded_rect(px, py, PANEL_W, PANEL_H, PANEL_R, 0xFF6A5ACD);

    graphics_draw_string(px + 100, py + 40, "Welcome, User!", 0xFFE0E0FF);

    int bx = px + (PANEL_W - BTN_W) / 2;
    int by = py + 200;
    btn_rect[0] = bx;
    btn_rect[1] = by;
    btn_rect[2] = BTN_W;
    btn_rect[3] = BTN_H;

    graphics_fill_rounded_rect(bx, by, BTN_W, BTN_H, BTN_R, 0xFF6C63FF);
    graphics_draw_rounded_rect(bx, by, BTN_W, BTN_H, BTN_R, 0xFF8B83FF);
    graphics_draw_string(bx + 56, by + 14, "  Sign In  ", 0xFFFFFFFF);
}

static void draw_mouse(void) {
    for (uint32_t row = 0; row < 16; ++row) {
        for (uint32_t col = 0; col < 16; ++col) {
            graphics_put_pixel(mouse_x + col, mouse_y + row, 0xFFFFFFFF);
        }
    }
}

void login_screen(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    graphics_fill_gradient_v(0, 0, w, h, 0xFF0D0D2B, 0xFF1A0A3E);

    uint32_t seed = 42;
    for (int i = 0; i < 150; i++) {
        seed = seed * 1103515245 + 12345;
        uint32_t sx = (seed >> 16) % w;
        seed = seed * 1103515245 + 12345;
        uint32_t sy = (seed >> 16) % h;
        seed = seed * 1103515245 + 12345;
        uint32_t b = ((seed >> 16) & 0x7F) + 0x80;
        graphics_put_pixel(sx, sy, 0xFF000000 | (b << 16) | (b << 8) | b);
    }

    draw_logo();
    draw_panel();

    save_cursor_bg(mouse_x, mouse_y);
    draw_mouse();
    prev_mx = mouse_x;
    prev_my = mouse_y;

    bool done = false;
    bool hovered = false;

    while (!done) {
        struct mouse_state ms;
        mouse_get_state(&ms);
        if (ms.dx || ms.dy) {
            if (prev_mx >= 0)
                restore_cursor_bg(prev_mx, prev_my);
            mouse_x += ms.dx;
            mouse_y += ms.dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)w) mouse_x = (int)w - 1;
            if (mouse_y >= (int)h) mouse_y = (int)h - 1;

            int bx = btn_rect[0], by = btn_rect[1];
            bool over = (mouse_x >= bx && mouse_x < bx + BTN_W && mouse_y >= by && mouse_y < by + BTN_H);
            if (over != hovered) {
                hovered = over;
                draw_panel();
                save_cursor_bg(mouse_x, mouse_y);
            } else {
                save_cursor_bg(mouse_x, mouse_y);
            }

            draw_mouse();
            prev_mx = mouse_x;
            prev_my = mouse_y;
        }

        if (ms.buttons & 0x01) {
            int bx = btn_rect[0], by = btn_rect[1];
            if (mouse_x >= bx && mouse_x < bx + BTN_W && mouse_y >= by && mouse_y < by + BTN_H) {
                done = true;
            }
        }

        if (keyboard_has_input()) {
            char key;
            if (keyboard_read_char(&key)) {
                done = true;
            }
        }

        __asm__ volatile("hlt");
    }
}
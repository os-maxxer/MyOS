#include <solis/login.h>
#include <solis/graphics.h>
#include <solis/mouse.h>
#include <solis/keyboard.h>

#define PANEL_W 380
#define PANEL_H 300
#define BTN_W 200
#define BTN_H 44

#define C_BG_TOP   0xFF2C2F33
#define C_BG_BOT   0xFF17181B
#define C_PANEL    0xFF24272B
#define C_PANEL_LN 0xFF1B1E22
#define C_TEXT     0xFFEAEAEA
#define C_TEXT_DIM 0xFF888888
#define C_ACTIVE   0xFF4C7BD9
#define C_ACTIVE_DK 0xFF3A63B3

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

static uint32_t lerp_color(uint32_t c1, uint32_t c2, int t, int max) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + ((r2 - r1) * t / max);
    uint8_t g = g1 + ((g2 - g1) * t / max);
    uint8_t b = b1 + ((b2 - b1) * t / max);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void draw_background(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    graphics_fill_gradient_v(0, 0, w, h, C_BG_TOP, C_BG_BOT);
    int cx = (int)w / 2;
    for (int r = 220; r > 0; r -= 4) {
        graphics_fill_circle(cx, (int)h * 3 / 4, r,
            lerp_color(0xFF3A5A86, C_BG_BOT, r, 220));
    }
}

static void draw_logo(void) {
    uint32_t w = graphics_get_width();
    uint32_t cx = (int)w / 2;
    int logo_y = 90;
    for (int r = 46; r > 36; r -= 2)
        graphics_fill_circle(cx, logo_y, r, lerp_color(0xFF4C7BD9, C_PANEL, r, 46));
    graphics_fill_circle(cx, logo_y, 34, C_ACTIVE);
    graphics_fill_circle(cx, logo_y, 26, 0xFF1B1E22);
    graphics_draw_string(cx - 24, logo_y - 44, "Solis", C_ACTIVE);
    graphics_draw_string(cx - 28, logo_y + 48, "Solis OS", 0xFFAFC4EE);
}

static void draw_panel(bool hovered) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    int px = ((int)w - PANEL_W) / 2;
    int py = ((int)h - PANEL_H) / 2 + 40;

    graphics_fill_rect(px - 2, py - 3, PANEL_W + 4, PANEL_H + 4, 0xFF101012);
    graphics_fill_rect(px, py, PANEL_W, PANEL_H, C_PANEL);
    graphics_draw_rect(px, py, PANEL_W, PANEL_H, C_PANEL_LN);

    graphics_draw_string(px + 100, py + 40, "Welcome, User!", C_TEXT);

    int bx = px + (PANEL_W - BTN_W) / 2;
    int by = py + 200;
    btn_rect[0] = bx;
    btn_rect[1] = by;
    btn_rect[2] = BTN_W;
    btn_rect[3] = BTN_H;

    uint32_t bc = hovered ? C_ACTIVE_DK : C_ACTIVE;
    graphics_fill_rect(bx, by, BTN_W, BTN_H, bc);
    graphics_draw_rect(bx, by, BTN_W, BTN_H, C_ACTIVE_DK);
    graphics_draw_string(bx + 56, by + 14, "  Sign In  ", 0xFFFFFFFF);
}

void login_screen(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();

    draw_background();
    draw_logo();
    draw_panel(false);

    save_cursor_bg(mouse_x, mouse_y);
    graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
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
                draw_panel(hovered);
            }
            save_cursor_bg(mouse_x, mouse_y);
            graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
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

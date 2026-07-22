#include <nyx/login.h>
#include <nyx/graphics.h>
#include <nyx/mouse.h>
#include <nyx/keyboard.h>

#define PANEL_W 420
#define PANEL_H 340
#define BTN_W 200
#define BTN_H 48

static int mouse_x = 512;
static int mouse_y = 384;
static int btn_rect[4];

static uint32_t lerp_color(uint32_t c1, uint32_t c2, int t, int max) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + ((r2 - r1) * t / max);
    uint8_t g = g1 + ((g2 - g1) * t / max);
    uint8_t b = b1 + ((b2 - b1) * t / max);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void draw_gradient_bg(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    uint32_t c1 = 0xFF0D0D2B;
    uint32_t c2 = 0xFF1A0A3E;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t color = lerp_color(c1, c2, y, h);
        graphics_fill_rect(0, y, w, 1, color);
    }
}

static void draw_stars(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    uint32_t seed = 42;
    for (int i = 0; i < 120; i++) {
        seed = seed * 1103515245 + 12345;
        uint32_t x = (seed >> 16) % w;
        seed = seed * 1103515245 + 12345;
        uint32_t y = (seed >> 16) % h;
        seed = seed * 1103515245 + 12345;
        uint32_t brightness = ((seed >> 16) & 0x7F) + 0x80;
        uint32_t color = 0xFF000000 | (brightness << 16) | (brightness << 8) | brightness;
        graphics_put_pixel(x, y, color);
    }
}

static void draw_nebula(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    int cx = (int)w / 3;
    int cy = (int)h / 4;
    for (int r = 80; r > 0; r -= 2) {
        uint32_t color = lerp_color(0x00000000, 0x223366FF, r, 80);
        graphics_fill_rect(cx - r, cy - r, r * 2, r * 2, color);
    }
    cx = (int)w * 2 / 3;
    cy = (int)h * 2 / 3;
    for (int r = 60; r > 0; r -= 2) {
        uint32_t color = lerp_color(0x00000000, 0x226633AA, r, 60);
        graphics_fill_rect(cx - r, cy - r, r * 2, r * 2, color);
    }
}

static void draw_panel(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    int px = ((int)w - PANEL_W) / 2;
    int py = ((int)h - PANEL_H) / 2 - 30;

    graphics_fill_rect(px, py, PANEL_W, PANEL_H, 0xDD1A1A2E);
    graphics_draw_rect(px, py, PANEL_W, PANEL_H, 0xFF6A5ACD);
    graphics_draw_rect(px - 1, py - 1, PANEL_W + 2, PANEL_H + 2, 0x33C0C0C0);

    graphics_draw_string(px + 60, py + 24, "  _   _   ___    ____  ", 0xFF9370DB);
    graphics_draw_string(px + 60, py + 40, " | \\ | | / _ \\  / ___| ", 0xFF9370DB);
    graphics_draw_string(px + 60, py + 56, " |  \\| || | | | \\___ \\ ", 0xFFB088F0);
    graphics_draw_string(px + 60, py + 72, " | |\\  || |_| | ___) |", 0xFFB088F0);
    graphics_draw_string(px + 60, py + 88, " |_| \\_| \\___/ |____/ ", 0xFFD4A0FF);

    graphics_draw_string(px + 112, py + 130, "Welcome, User!", 0xFFE0E0FF);

    int bx = px + (PANEL_W - BTN_W) / 2;
    int by = py + 200;
    btn_rect[0] = bx;
    btn_rect[1] = by;
    btn_rect[2] = BTN_W;
    btn_rect[3] = BTN_H;

    graphics_fill_rect(bx, by, BTN_W, BTN_H, 0xFF4A3BA0);
    graphics_draw_rect(bx, by, BTN_W, BTN_H, 0xFF7B68EE);
    graphics_draw_string(bx + 48, by + 16, "  Sign In  ", 0xFFFFFFFF);
}

static void draw_mouse(void) {
    for (uint32_t row = 0; row < 16; ++row) {
        for (uint32_t col = 0; col < 16; ++col) {
            graphics_put_pixel(mouse_x + col, mouse_y + row, 0xFFFFFFFF);
        }
    }
}

void login_screen(void) {
    draw_gradient_bg();
    draw_nebula();
    draw_stars();
    draw_panel();

    bool done = false;
    bool hovered = false;
    int last_mx = mouse_x;
    int last_my = mouse_y;

    while (!done) {
        struct mouse_state ms;
        mouse_get_state(&ms);
        if (ms.dx || ms.dy) {
            graphics_fill_rect(last_mx, last_my, 16, 16, 0xFF0D0D2B);
            mouse_x += ms.dx;
            mouse_y += ms.dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
            if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;

            int bx = btn_rect[0], by = btn_rect[1];
            bool over = (mouse_x >= bx && mouse_x < bx + BTN_W && mouse_y >= by && mouse_y < by + BTN_H);
            if (over != hovered) {
                hovered = over;
                draw_panel();
            }

            draw_mouse();
            last_mx = mouse_x;
            last_my = mouse_y;
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

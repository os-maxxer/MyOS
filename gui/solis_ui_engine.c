#include <solis/solis_ui_engine.h>
#include <solis/graphics.h>

#define BG_TOP    0xFF070C18
#define BG_BOTTOM 0xFF0D1727
#define BG_PANEL  0xFF101B2D
#define BG_LINE   0xFF1B2B42
#define ACCENT    0xFF3C6FD6
#define VIOLET    0xFF3B2A5C
#define TEAL      0xFF12515A

static uint32_t mix(uint32_t a, uint32_t b, int t, int max) {
    if (max <= 0) return a;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * t / max;
    int g = ag + (bg - ag) * t / max;
    int bl = ab + (bb - ab) * t / max;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void glow(int cx, int cy, int radius, uint32_t color) {
    if (radius < 2) return;
    for (int r = radius; r > 0; r -= 4) {
        uint32_t c = mix(BG_BOTTOM, color, radius - r, radius);
        graphics_fill_circle((uint32_t)cx, (uint32_t)cy, (uint32_t)r, c);
    }
}

void solis_ui_engine_draw_wallpaper(void) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    if (w <= 0 || h <= 0) return;

    graphics_fill_gradient_v(0, 0, (uint32_t)w, (uint32_t)h,
                             BG_TOP, BG_BOTTOM);

    glow(w * 16 / 100, h * 20 / 100, h * 38 / 100, ACCENT);
    glow(w * 87 / 100, h * 27 / 100, h * 30 / 100, VIOLET);
    glow(w * 53 / 100, h * 96 / 100, h * 34 / 100, TEAL);

    int ring_x = w * 72 / 100;
    int ring_y = h * 52 / 100;
    int ring_r = h * 29 / 100;
    graphics_draw_circle((uint32_t)ring_x, (uint32_t)ring_y,
                         (uint32_t)ring_r, BG_LINE);
    graphics_draw_circle((uint32_t)ring_x, (uint32_t)ring_y,
                         (uint32_t)(ring_r - 8), 0xFF142238);

    uint32_t seed = 0x51A15u;
    for (int i = 0; i < 46; i++) {
        seed = seed * 1664525u + 1013904223u;
        int sx = (int)((seed >> 8) % (uint32_t)(w * 60 / 100));
        seed = seed * 1664525u + 1013904223u;
        int sy = (int)((seed >> 8) % (uint32_t)(h * 55 / 100));
        graphics_put_pixel((uint32_t)sx, (uint32_t)sy,
                           (i & 3) ? 0xFF1A2A42 : 0xFF2A4264);
    }
}

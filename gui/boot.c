#include <solis/boot.h>
#include <solis/graphics.h>
#include <solis/timer.h>
#include <solis/brand.h>
#include <stdbool.h>

#define BOOT_MAX_LINES 10
#define BOOT_STAGES    9

#define C_BOOT_BG     0xFF070C18
#define C_BOOT_BG2    0xFF0E1728
#define C_BOOT_PANEL  0xFF111A2B
#define C_BOOT_LINE   0xFF28364D
#define C_BOOT_TEXT   0xFFF2F7FF
#define C_BOOT_DIM    0xFF96A6BC
#define C_BOOT_BLUE   0xFF5B8DEF
#define C_BOOT_TEAL   0xFF4FD1B0
#define C_BOOT_GLOW   0xFF1B3158

static char boot_lines[BOOT_MAX_LINES][64];
static int boot_line_count;
static int boot_stage;
static int boot_enabled;
static int boot_bg_w;
static int boot_bg_h;

static int boot_text_len(const char *text) {
    int n = 0;
    while (text[n]) n++;
    return n;
}

static void boot_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t boot_mix(uint32_t a, uint32_t b, int t, int max) {
    if (max <= 0) return a;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * t / max;
    int g = ag + (bg - ag) * t / max;
    int bl = ab + (bb - ab) * t / max;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void boot_glow(int cx, int cy, int radius, uint32_t color) {
    if (radius < 1) return;
    for (int r = radius; r > 0; r -= 5) {
        uint32_t c = boot_mix(color, C_BOOT_BG, radius - r, radius);
        graphics_fill_circle((uint32_t)cx, (uint32_t)cy, (uint32_t)r, c);
    }
}

static void boot_draw_background(void) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    boot_bg_w = w;
    boot_bg_h = h;

    graphics_fill_gradient_v(0, 0, (uint32_t)w, (uint32_t)h,
                             C_BOOT_BG, C_BOOT_BG2);
    boot_glow(w * 15 / 100, h * 18 / 100, h * 34 / 100, C_BOOT_GLOW);
    boot_glow(w * 86 / 100, h * 24 / 100, h * 28 / 100, 0xFF2B2350);
    boot_glow(w * 52 / 100, h * 96 / 100, h * 30 / 100, 0xFF123C45);

    graphics_draw_circle((uint32_t)(w * 86 / 100),
                         (uint32_t)(h * 18 / 100),
                         (uint32_t)(h / 5), 0xFF1A2941);
    graphics_draw_circle((uint32_t)(w * 14 / 100),
                         (uint32_t)(h * 83 / 100),
                         (uint32_t)(h / 4), 0xFF1A2941);
}

static void boot_draw_mark(int x, int y, int size) {
    int stroke = size / 8;
    if (stroke < 2) stroke = 2;
    int left = x;
    int top = y;
    int right = x + size;
    int mid = y + size / 2 - stroke / 2;
    int bottom = y + size;

    graphics_fill_rect((uint32_t)left, (uint32_t)top,
                       (uint32_t)size, (uint32_t)stroke, C_BOOT_BLUE);
    graphics_fill_rect((uint32_t)left, (uint32_t)top,
                       (uint32_t)stroke, (uint32_t)(size / 2 + stroke),
                       C_BOOT_BLUE);
    graphics_fill_rect((uint32_t)left, (uint32_t)mid,
                       (uint32_t)size, (uint32_t)stroke, C_BOOT_BLUE);
    graphics_fill_rect((uint32_t)(right - stroke), (uint32_t)mid,
                       (uint32_t)stroke, (uint32_t)(size / 2),
                       C_BOOT_BLUE);
    graphics_fill_rect((uint32_t)left, (uint32_t)(bottom - stroke),
                       (uint32_t)size, (uint32_t)stroke, C_BOOT_BLUE);
}

static void boot_draw(void) {
    if (!boot_enabled) return;

    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    if (w <= 0 || h <= 0) return;
    boot_bg_w = w;
    boot_bg_h = h;

    bool staged = graphics_begin_frame();
    boot_draw_background();

    int panel_w = w * 76 / 100;
    if (panel_w > 900) panel_w = 900;
    if (panel_w < 520) panel_w = w - 48;
    int panel_h = h * 55 / 100;
    if (panel_h > 500) panel_h = 500;
    if (panel_h < 330) panel_h = h - 160;
    int px = (w - panel_w) / 2;
    int py = h * 27 / 100;
    if (py + panel_h > h - 48) py = h - 48 - panel_h;

    int mark_size = h / 20;
    if (mark_size < 24) mark_size = 24;
    if (mark_size > 44) mark_size = 44;
    boot_draw_mark(px + 4, py - mark_size - 26, mark_size);
    graphics_draw_string((uint32_t)(px + mark_size + 16),
                         (uint32_t)(py - mark_size - 20),
                         SOLIS_RELEASE_LINE, C_BOOT_TEXT);
    graphics_draw_string((uint32_t)(px + mark_size + 16),
                         (uint32_t)(py - mark_size - 2),
                         SOLIS_TAGLINE, C_BOOT_DIM);

    const char *tag = "64-BIT  /  UEFI  /  EXTRA READY";
    int tag_len = boot_text_len(tag);
    graphics_draw_string((uint32_t)(px + panel_w - tag_len * 8),
                         (uint32_t)(py - mark_size - 8), tag, C_BOOT_DIM);

    graphics_fill_rounded_rect((uint32_t)(px + 5), (uint32_t)(py + 6),
                               (uint32_t)panel_w, (uint32_t)panel_h,
                               18, 0xFF050912);
    graphics_fill_rounded_rect((uint32_t)px, (uint32_t)py,
                               (uint32_t)panel_w, (uint32_t)panel_h,
                               18, C_BOOT_PANEL);
    graphics_draw_rounded_rect((uint32_t)px, (uint32_t)py,
                               (uint32_t)panel_w, (uint32_t)panel_h,
                               18, C_BOOT_LINE);
    graphics_fill_rect((uint32_t)(px + 18), (uint32_t)(py + 1),
                       (uint32_t)(panel_w - 36), 2, C_BOOT_BLUE);

    graphics_draw_string((uint32_t)(px + 24), (uint32_t)(py + 24),
                         SOLIS_NAME " " SOLIS_VERSION
                         "  /  SYSTEM INITIALIZATION", C_BOOT_DIM);
    graphics_draw_string((uint32_t)(px + 24), (uint32_t)(py + 42),
                         boot_line_count ? "Bringing up the Solis runtime"
                                         : "Starting...",
                         C_BOOT_TEXT);

    int line_y = py + 78;
    for (int i = 0; i < boot_line_count; i++) {
        graphics_fill_rounded_rect((uint32_t)(px + 26),
                                   (uint32_t)(line_y + 5),
                                   12, 12, 6, C_BOOT_TEAL);
        graphics_fill_rect((uint32_t)(px + 28), (uint32_t)(line_y + 7),
                           8, 8, C_BOOT_PANEL);
        graphics_fill_circle((uint32_t)(px + 32), (uint32_t)(line_y + 11),
                             3, C_BOOT_TEAL);
        graphics_draw_string((uint32_t)(px + 48), (uint32_t)line_y,
                             boot_lines[i], C_BOOT_TEXT);
        line_y += 27;
    }

    int bar_x = px + 24;
    int bar_y = py + panel_h - 38;
    int bar_w = panel_w - 48;
    graphics_fill_rounded_rect((uint32_t)bar_x, (uint32_t)bar_y,
                               (uint32_t)bar_w, 8, 4, 0xFF1A2A40);
    int progress = boot_stage * 100 / BOOT_STAGES;
    if (progress > 100) progress = 100;
    int fill = bar_w * progress / 100;
    if (fill > 0)
        graphics_fill_rounded_rect((uint32_t)bar_x, (uint32_t)bar_y,
                                   (uint32_t)fill, 8, 4, C_BOOT_BLUE);

    char pct[5];
    int pct_len = 0;
    if (progress >= 100) {
        pct[pct_len++] = '1';
        pct[pct_len++] = '0';
        pct[pct_len++] = '0';
    } else if (progress >= 10) {
        pct[pct_len++] = '0' + (char)(progress / 10);
        pct[pct_len++] = '0' + (char)(progress % 10);
    } else {
        pct[pct_len++] = '0' + (char)progress;
    }
    pct[pct_len++] = '%';
    pct[pct_len] = '\0';
    int pct_x = bar_x + bar_w - pct_len * 8;
    graphics_draw_string((uint32_t)pct_x, (uint32_t)(bar_y - 18), pct, C_BOOT_DIM);
    graphics_draw_string((uint32_t)bar_x, (uint32_t)(bar_y - 18),
                         "WELCOME", C_BOOT_DIM);
    if (staged) graphics_end_frame();
}

void boot_report_begin(void) {
    boot_lines[0][0] = '\0';
    boot_line_count = 0;
    boot_stage = 0;
    boot_enabled = graphics_is_ready();
}

void boot_report_step(const char *message) {
    if (!boot_enabled || !message) return;
    if (boot_line_count < BOOT_MAX_LINES) {
        boot_copy(boot_lines[boot_line_count], message, 64);
        boot_line_count++;
    } else {
        for (int i = 1; i < BOOT_MAX_LINES; i++) {
            boot_copy(boot_lines[i - 1], boot_lines[i], 64);
        }
        boot_copy(boot_lines[BOOT_MAX_LINES - 1], message, 64);
    }
    if (boot_stage < BOOT_STAGES) boot_stage++;
    boot_draw();
}

void boot_report_finish(void) {
    if (!boot_enabled) return;
    boot_report_step("Desktop session ready - welcome to "
                     SOLIS_NAME " " SOLIS_VERSION);
    uint32_t start = timer_get_ticks();
    while ((uint32_t)(timer_get_ticks() - start) < 180) {
        __asm__ volatile("hlt");
    }
    boot_enabled = 0;
}

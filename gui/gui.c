/*
 * Solis OS desktop environment - Linux-inspired flat shell with BoredOS
 * utility and geometry.
 *
 * Direct framebuffer rendering. Flat rectangles, 1px borders, bitmap
 * fonts, crisp control glyphs, and a subtle modern palette inspired by
 * Linux desktops and classic BoredOS simplicity.
 */

#include <solis/gui.h>
#include <solis/graphics.h>
#include <solis/console.h>
#include <solis/mouse.h>
#include <solis/pointer.h>
#include <solis/keyboard.h>
#include <solis/bmp.h>
#include <solis/spx.h>
#include <solis/vfs.h>
#include <solis/rtc.h>
#include <solis/ports.h>
#include <solis/solfs.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */
#define MAX_WINDOWS   8
#define TITLEBAR_H    24
#define WIN_BORDER    1
#define BTN_W         22          /* window control button width  */
#define RESIZE_MARGIN 5
#define MIN_WIN_W     240
#define MIN_WIN_H     140
#define MENU_W        200
#define MENU_ITEM_H   24
#define MENU_HEADER_H 30
#define MENU_SEC_H    18
#define SNAP_DIST     8
/* How far the window drop shadow bleeds past the window rect, to the right
 * and below only. Damage for a window has to include this or the shadow
 * edge gets left behind when the window moves. */
#define WIN_SHADOW_EXTENT 4

/* Taskbar card geometry. PANEL_H is the strip of screen the taskbar
 * reserves at the bottom, so windows never end up underneath it. */
#define TASKBAR_H        46
#define TASKBAR_MARGIN    8
#define TASKBAR_SIDE     10
#define TASKBAR_RADIUS   12
#define PANEL_H          (TASKBAR_H + TASKBAR_MARGIN)

/* ------------------------------------------------------------------ */
/* Palette (BoredOS-inspired, ~14 colors)                              */
/* ------------------------------------------------------------------ */
#define C_BG          0xFF6673A8  /* BoredOS periwinkle field          */
#define C_PANEL       0xFF171E4B  /* deep cobalt panel / menu           */
#define C_PANEL_LINE  0xFF303A78  /* panel bevel / border               */
#define C_ACTIVE      0xFF596DE8  /* cobalt accent                      */
#define C_ACTIVE_DK   0xFF394BB8  /* pressed cobalt                     */
#define C_INACTIVE    0xFF1B2430  /* inactive window titlebar         */
#define C_INACTIVE_DK 0xFF101924  /* inactive border                  */
#define C_WIN_BG      0xFF101821  /* window body                      */
#define C_TEXT        0xFFEAF3FF  /* primary text                     */
#define C_TEXT_DIM    0xFF94A3B8  /* secondary text / disabled        */
#define C_BTN         0xFF1E293B  /* flat button fill                 */
#define C_BTN_HOVER   0xFF2A3A4D  /* flat button hover                */
#define C_BTN_PRESS   0xFF17212C  /* flat button pressed              */
#define C_ERROR       0xFFD9534F
#define C_OK          0xFF5CB85C
#define C_AMBER       0xFFD9A441

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
struct gui_window {
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
    bool dragging;
    bool resizing;
    int  resize_flags;              /* bit0 right edge, bit1 bottom   */
    int  drag_off_x;
    int  drag_off_y;
    int  resize_ox, resize_oy;      /* pointer pos at resize start    */
    int  resize_ow, resize_oh;      /* bounds at resize start         */
    struct gui_rect bounds;
    struct gui_rect restore;        /* pre-maximize bounds            */
    char title[24];
    int  app_id;
};

static struct gui_window windows[MAX_WINDOWS];
static int zorder[MAX_WINDOWS];     /* zorder[0] = topmost window     */
static int zcount = 0;

static int mouse_x = 400;
static int mouse_y = 300;
static uint8_t mouse_buttons = 0;
static bool gui_mouse_prev_left = false;
static bool gui_mouse_prev_right = false;
static int active_window = -1;
static int drag_window = -1;
static bool redraw_pending = true;

static bool start_menu_open = false;
static int  menu_hover = -1;
static int  hover_logo = 0;
static int  hover_task = -1;

static bool logout_requested = false;

static int gui_theme = GUI_THEME_BORED;
static uint32_t bg_color = C_BG;

/* The procedural desktop backdrops are expensive, so they (plus the desktop
 * icons) are rendered once into a cache and blitted on every redraw. */
#define BG_CACHE_MAX_W 1280
#define BG_CACHE_MAX_H 960
static uint32_t bg_cache[BG_CACHE_MAX_W * BG_CACHE_MAX_H];
static uint32_t bg_cache_w = 0;
static uint32_t bg_cache_h = 0;
static bool bg_cache_valid = false;
static bool bg_cache_usable = false;

#define CURSOR_SIZE 16
static int prev_cursor_x = -1;
static int prev_cursor_y = -1;

/* ------------------------------------------------------------------ */
/* Damage tracking                                                     */
/*                                                                     */
/* Instead of a "repaint everything" flag, every change records the     */
/* screen rectangle it affected. The repaint walks only those rects and */
/* uses the graphics clip stack so each layer draws into its own slice  */
/* of the screen. When the damage budget is exceeded we collapse to one */
/* full-screen rect: correct, just slower, and far less code than a     */
/* perfect region tracker.                                             */
/* ------------------------------------------------------------------ */
#define MAX_DAMAGE 24
static struct gui_rect damage[MAX_DAMAGE];
static int damage_n = 0;
static bool damage_all = false;

/* Set to 0 to silence the per-repaint [GFX] line once the numbers are trusted. */
#define GUI_REDRAW_TRACE 1
/* Set to 1 to also dump each damage rect; handy when a repaint is larger
 * than it should be. Off by default because it triples the serial traffic. */
#define GUI_REDRAW_TRACE_DUMP 0
static uint32_t last_paint_writes = 0;
static int paint_rects = 0;
static int paint_was_all = 0;
static int last_paint_n = 0;
static struct gui_rect last_paint[8];

static void gui_damage_all(void) {
    damage_all = true;
    damage_n = 0;
    redraw_pending = true;
}

/* Record a rect as needing repaint. Clamped to the screen and merged into
 * the last entry when the two are adjacent, so a drag or a hover sweep
 * usually collapses into a single rect instead of one per mouse report. */
static void gui_damage(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int sw = (int)graphics_get_width();
    int sh = (int)graphics_get_height();
    if (x >= sw || y >= sh) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return;

    redraw_pending = true;
    if (damage_all) return;

    /* Merge with the most recent rect if they touch and the union stays
     * smaller than the two parts plus a small overhead. */
    if (damage_n > 0) {
        struct gui_rect *p = &damage[damage_n - 1];
        int lx = p->x < x ? p->x : x;
        int ly = p->y < y ? p->y : y;
        int rx = (p->x + p->width) > (x + w) ? p->x + p->width : x + w;
        int ry = (p->y + p->height) > (y + h) ? p->y + p->height : y + h;
        int uarea = (rx - lx) * (ry - ly);
        int parea = p->width * p->height;
        if (uarea <= parea + w * h + 4096) {
            p->x = lx; p->y = ly;
            p->width = rx - lx; p->height = ry - ly;
            return;
        }
    }

    if (damage_n >= MAX_DAMAGE) { gui_damage_all(); return; }
    damage[damage_n].x = x;
    damage[damage_n].y = y;
    damage[damage_n].width = w;
    damage[damage_n].height = h;
    damage_n++;
}

/* Expand a rect outward by n pixels on every side. Used for shadows and
 * for the cursor, whose black outline spills one pixel past the glyph. */
static void gui_damage_pad(int x, int y, int w, int h, int n) {
    gui_damage(x - n, y - n, w + 2 * n, h + 2 * n);
}

/* Per-layer helpers, defined next to the painter that consumes the damage. */
static void gui_damage_window(int idx);
static void gui_damage_active_content(void);
static void gui_damage_taskbar(void);
static void gui_damage_tray(void);
static void gui_damage_menu(void);
static void gui_damage_cursor(int x, int y);

/* Damage the smallest box containing both rects. Moving or resizing a window
 * leaves pixels behind at the old geometry as well as new ones to fill. */
static void gui_damage_union(const struct gui_rect *a, const struct gui_rect *b) {
    int lx = a->x < b->x ? a->x : b->x;
    int ly = a->y < b->y ? a->y : b->y;
    int rx = (a->x + a->width) > (b->x + b->width) ? a->x + a->width : b->x + b->width;
    int ry = (a->y + a->height) > (b->y + b->height) ? a->y + a->height : b->y + b->height;
    gui_damage(lx, ly, rx - lx + WIN_SHADOW_EXTENT, ry - ly + WIN_SHADOW_EXTENT);
}

/* ------------------------------------------------------------------ */
/* App icons                                                           */
/* ------------------------------------------------------------------ */
static const struct {
    const char *name;
    uint32_t color;
} app_icons[9] = {
    {"Helios",       0xFFFFB84D},
    {"Notes",        0xFF4C7BD9},
    {"Paint",        0xFFE0576B},
    {"Settings",     0xFFD9A441},
    {"Files",        0xFF3FA06A},
    {"Task Manager", 0xFFB055E0},
    {"SOLPKG",       0xFF46A8E8},
    {"Editor",       0xFF35B79B},
    {"Tetris",       0xFFE0863C},
};

static void draw_line(int x0, int y0, int x1, int y1, uint32_t c);

static void draw_app_icon(int x, int y, int size, int slot) {
    if (slot < 0 || slot > 8) return;
    int u = size / 14;
    if (u < 1) u = 1;
    uint32_t ink = 0xFFF8FAFF;
    graphics_fill_rounded_rect(x, y, size, size, size / 5, app_icons[slot].color);
    graphics_draw_rounded_rect(x, y, size, size, size / 5, 0x66000000);

    switch (slot) {
        case 0: { /* terminal screen and prompt */
            int sx = x + size / 5, sy = y + size / 5;
            int sw = size * 3 / 5, sh = size * 3 / 5;
            graphics_draw_rect(sx, sy, sw, sh, ink);
            draw_line(sx + u * 2, sy + sh / 2, sx + sw / 3, sy + sh * 2 / 3, ink);
            draw_line(sx + sw / 3, sy + sh * 2 / 3, sx + u * 2, sy + sh * 5 / 6, ink);
            graphics_fill_rect(sx + sw / 2, sy + sh * 2 / 3, sw / 4, u, ink);
            break;
        }
        case 1: { /* ruled notebook page */
            int px = x + size / 4, py = y + size / 6;
            int pw = size / 2, ph = size * 2 / 3;
            graphics_fill_rect(px, py, pw, ph, 0xFFF2F6FF);
            graphics_draw_rect(px, py, pw, ph, 0xFF263B66);
            for (int r = 1; r <= 3; r++)
                graphics_fill_rect(px + u * 2, py + r * ph / 5, pw - u * 4, u, app_icons[slot].color);
            break;
        }
        case 2: { /* artist palette and brush */
            int cx = x + size / 2, cy = y + size / 2;
            graphics_fill_circle(cx, cy, size * 3 / 10, 0xFFFFF4E6);
            graphics_fill_circle(cx - size / 10, cy - size / 10, u, 0xFFE0576B);
            graphics_fill_circle(cx + size / 10, cy - size / 10, u, 0xFF4C7BD9);
            graphics_fill_circle(cx - size / 10, cy + size / 12, u, 0xFF43A878);
            graphics_fill_circle(cx + size / 10, cy + size / 12, u, 0xFFFFB84D);
            draw_line(cx + size / 8, cy + size / 4, x + size * 4 / 5, y + size / 5, ink);
            break;
        }
        case 3: { /* gear */
            int cx = x + size / 2, cy = y + size / 2;
            int r = size / 3;
            graphics_fill_circle(cx, cy, r, ink);
            for (int a = -1; a <= 1; a++) {
                graphics_fill_rect(cx - u, cy - size * 2 / 5 + a * u * 3, u * 2, u * 2, ink);
                graphics_fill_rect(cx - u, cy + size / 3 + a * u * 3, u * 2, u * 2, ink);
                graphics_fill_rect(cx - size * 2 / 5 + a * u * 3, cy - u, u * 2, u * 2, ink);
                graphics_fill_rect(cx + size / 3 + a * u * 3, cy - u, u * 2, u * 2, ink);
            }
            graphics_fill_circle(cx, cy, size / 8, app_icons[slot].color);
            break;
        }
        case 4: { /* folder */
            int fx = x + size / 6, fy = y + size / 3;
            graphics_fill_rect(fx, fy, size * 2 / 3, size * 2 / 5, 0xFFFFD979);
            graphics_fill_rect(fx + u, fy - size / 8, size / 3, size / 6, 0xFFFFE69A);
            graphics_draw_rect(fx, fy, size * 2 / 3, size * 2 / 5, 0xFF62451C);
            break;
        }
        case 5: { /* live chart */
            int bx = x + size / 4, by = y + size * 3 / 4;
            graphics_fill_rect(bx, by, size / 9, size / 4, ink);
            graphics_fill_rect(bx + size / 5, by - size / 4, size / 9, size / 2, ink);
            graphics_fill_rect(bx + size * 2 / 5, by - size * 2 / 5, size / 9, size * 3 / 5, ink);
            draw_line(bx, by - size / 3, bx + size / 4, by - size / 2, 0xFFFFF1B8);
            draw_line(bx + size / 4, by - size / 2, bx + size / 2, by - size / 3, 0xFFFFF1B8);
            break;
        }
        case 6: { /* open software package */
            int cx = x + size / 2, top = y + size / 4, bot = y + size * 3 / 4;
            draw_line(cx, top, x + size / 5, top + size / 8, ink);
            draw_line(cx, top, x + size * 4 / 5, top + size / 8, ink);
            draw_line(x + size / 5, top + size / 8, x + size / 5, bot, ink);
            draw_line(x + size * 4 / 5, top + size / 8, x + size * 4 / 5, bot, ink);
            draw_line(x + size / 5, bot, cx, y + size * 7 / 8, ink);
            draw_line(x + size * 4 / 5, bot, cx, y + size * 7 / 8, ink);
            draw_line(cx, top, cx, y + size * 3 / 5, ink);
            break;
        }
        case 7: { /* source page with pencil */
            int px = x + size / 5, py = y + size / 6;
            graphics_draw_rect(px, py, size / 2, size * 2 / 3, ink);
            for (int r = 0; r < 3; r++)
                graphics_fill_rect(px + u * 2, py + size / 3 + r * u * 2, size / 3, u, ink);
            draw_line(x + size / 2, y + size * 4 / 5, x + size * 4 / 5, y + size / 3, 0xFFFFF2C4);
            break;
        }
        case 8: { /* four-block puzzle */
            int b = size / 4, sx = x + size / 4, sy = y + size / 4;
            graphics_fill_rect(sx, sy, b, b, ink);
            graphics_fill_rect(sx + b, sy, b, b, 0xFFFFE39A);
            graphics_fill_rect(sx + b, sy + b, b, b, ink);
            graphics_fill_rect(sx + b * 2, sy + b, b, b, 0xFFFFE39A);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Z-order                                                             */
/* ------------------------------------------------------------------ */
static void z_remove(int idx) {
    int found = 0;
    for (int i = 0; i < zcount; i++) {
        if (zorder[i] == idx) found = 1;
        if (found && i + 1 < zcount) zorder[i] = zorder[i + 1];
    }
    if (found) zcount--;
}

static void z_raise(int idx) {
    z_remove(idx);
    for (int i = zcount; i > 0; i--) zorder[i] = zorder[i - 1];
    zorder[0] = idx;
    zcount++;
}

/* ------------------------------------------------------------------ */
/* Focus helpers                                                       */
/* ------------------------------------------------------------------ */
static void gui_focus_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    /* The previously focused window loses its highlight, and the z-order
     * swap can expose whatever was underneath, so repaint both. */
    for (int k = 0; k < MAX_WINDOWS; k++)
        if (windows[k].focused) gui_damage_window(k);
    active_window = idx;
    for (int k = 0; k < MAX_WINDOWS; k++)
        windows[k].focused = (k == idx);
    z_raise(idx);
    gui_damage_window(idx);
}

static void gui_focus_top(void) {
    for (int zi = 0; zi < zcount; zi++) {
        int i = zorder[zi];
        if (windows[i].visible && !windows[i].minimized) {
            gui_focus_window(i);
            return;
        }
    }
    for (int k = 0; k < MAX_WINDOWS; k++) {
        if (windows[k].focused) gui_damage_window(k);
        windows[k].focused = false;
    }
    active_window = -1;
    redraw_pending = true;
}

static void gui_cycle_focus(void) {
    int vis[MAX_WINDOWS];
    int n = 0;
    for (int zi = 0; zi < zcount && n < MAX_WINDOWS; zi++) {
        int i = zorder[zi];
        if (windows[i].visible && !windows[i].minimized) vis[n++] = i;
    }
    if (n == 0) return;
    int cur = 0;
    for (int i = 0; i < n; i++)
        if (vis[i] == active_window) { cur = i; break; }
    gui_focus_window(vis[(cur + 1) % n]);
}

/* ------------------------------------------------------------------ */
/* Window primitives                                                   */
/* ------------------------------------------------------------------ */
static void draw_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        graphics_put_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Control button glyphs (monochrome bitmap icons) */
static void glyph_min(int x, int y, uint32_t c) {
    graphics_fill_rect(x + 5, y + TITLEBAR_H - 7, BTN_W - 10, 2, c);
}

static void glyph_max(int x, int y, uint32_t c) {
    graphics_draw_rect(x + 6, y + 7, BTN_W - 12, BTN_W - 12, c);
}

static void glyph_restore(int x, int y, uint32_t c) {
    graphics_draw_rect(x + 8, y + 9, 7, 7, c);
    graphics_draw_rect(x + 6, y + 7, 7, 7, c);
}

static void glyph_close(int x, int y, uint32_t c) {
    draw_line(x + 6, y + 7, x + BTN_W - 6, y + TITLEBAR_H - 7, c);
    draw_line(x + BTN_W - 6, y + 7, x + 6, y + TITLEBAR_H - 7, c);
}

static void gui_draw_ctrl_btn(int x, int y, uint32_t color,
                              void (*glyph)(int, int, uint32_t)) {
    graphics_fill_circle(x + BTN_W / 2, y + TITLEBAR_H / 2, 7, color);
    glyph(x, y, 0xFF252A34);
}

static uint32_t blend_pixel(uint32_t base, uint32_t over, int alpha) {
    /* alpha: 0..32, weight of over */
    int br = (base >> 16) & 0xFF, bg = (base >> 8) & 0xFF, bb = base & 0xFF;
    int orc = (over >> 16) & 0xFF, og = (over >> 8) & 0xFF, ob = over & 0xFF;
    int r = (br * (32 - alpha) + orc * alpha) / 32;
    int g = (bg * (32 - alpha) + og * alpha) / 32;
    int b = (bb * (32 - alpha) + ob * alpha) / 32;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Soft drop shadow cast by a window on whatever sits behind it. */
static void gui_draw_window_shadow(int x, int y, int w, int h) {
    const int extent = WIN_SHADOW_EXTENT;
    uint32_t fb_w = graphics_get_width();
    uint32_t fb_h = graphics_get_height();
    for (int off = 1; off <= extent; off++) {
        int alpha = 13 - off * 3;   /* innermost darkest, fades outward */
        if (alpha <= 0) continue;
        int sx = x + w + off - 1;
        int sy = y + off - 1;
        if (sx < (int)fb_w) {
            for (int yy = y + 5; yy < y + h; yy++) {
                if (yy >= (int)fb_h) break;
                graphics_put_pixel(sx, yy,
                    blend_pixel(graphics_get_pixel(sx, yy), 0xFF000000, alpha));
            }
        }
        if (sy < (int)fb_h) {
            for (int xx = x + 5; xx < x + w; xx++) {
                if (xx >= (int)fb_w) break;
                graphics_put_pixel(xx, sy,
                    blend_pixel(graphics_get_pixel(xx, sy), 0xFF000000, alpha));
            }
        }
        if (sx < (int)fb_w && sy < (int)fb_h)
            graphics_put_pixel(sx, sy,
                blend_pixel(graphics_get_pixel(sx, sy), 0xFF000000, alpha));
    }
}

static void gui_draw_window(struct gui_window *w) {
    if (!w->visible || w->minimized) return;
    int x = w->bounds.x, y = w->bounds.y;
    int ww = w->bounds.width, hh = w->bounds.height;

    uint32_t bar    = w->focused ? C_ACTIVE : C_INACTIVE;
    uint32_t bar_top = w->focused ? 0xFF6FB6FF : 0xFF212C3A;
    uint32_t border = w->focused ? C_ACTIVE_DK : C_INACTIVE_DK;
    uint32_t tcol   = w->focused ? 0xFFFFFFFF : 0xFFB4B8BF;

    /* soft shadow under the window */
    gui_draw_window_shadow(x, y, ww, hh);

    /* body */
    graphics_fill_rect(x, y, ww, hh, C_WIN_BG);
    /* titlebar: subtle vertical gradient for a little depth */
    graphics_fill_gradient_v(x, y, ww, TITLEBAR_H, bar_top, bar);
    /* 1px border all around */
    graphics_draw_rect(x, y, ww, hh, border);

    /* left-aligned title */
    graphics_draw_string(x + 8, y + (TITLEBAR_H - 16) / 2, w->title, tcol);

    /* control buttons: [min] [max] [close] at top-right */
    int bx = x + ww - 3 * BTN_W - 1;
    int by = y;
    gui_draw_ctrl_btn(bx, by, 0xFFFFBD2E, glyph_min);
    gui_draw_ctrl_btn(bx + BTN_W, by, 0xFF28C840,
                      w->maximized ? glyph_restore : glyph_max);
    gui_draw_ctrl_btn(bx + 2 * BTN_W, by, 0xFFFF5F57, glyph_close);
}

static void gui_draw_window_content(struct gui_window *w) {
    if (!w->visible || w->minimized) return;
    int x = w->bounds.x, y = w->bounds.y;
    int ww = w->bounds.width, hh = w->bounds.height;
    int cx = x + WIN_BORDER;
    int cy = y + TITLEBAR_H;
    int cw = ww - 2 * WIN_BORDER;
    int ch = hh - TITLEBAR_H - WIN_BORDER;
    if (cw <= 0 || ch <= 0) return;
    graphics_fill_rect(cx, cy, cw, ch, C_WIN_BG);
    /* The app only gets the client rect. Anything it draws outside this
     * region is discarded, which is what keeps long labels and lists from
     * spilling over the window frame. */
    graphics_push_clip((uint32_t)cx, (uint32_t)cy, (uint32_t)cw, (uint32_t)ch);
    const struct app_exports *app = spx_get_exports(w->app_id - 1);
    if (app && app->draw) app->draw(cx, cy, cw, ch);
    graphics_pop_clip();
}

/* ------------------------------------------------------------------ */
/* Desktop / wallpapers                                                */
/* ------------------------------------------------------------------ */
static uint32_t lerp_color(uint32_t c1, uint32_t c2, int t, int max) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + ((r2 - r1) * t / max);
    uint8_t g = g1 + ((g2 - g1) * t / max);
    uint8_t b = b1 + ((b2 - b1) * t / max);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void fill_facet_triangle(int x1, int y1, int x2, int y2,
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

static void draw_solis_mark(int cx, int cy, int scale, uint32_t color) {
    int stroke = scale / 8;
    int left = cx - scale / 3;
    int top = cy - scale / 2;
    int mid = cy - stroke / 2;
    int right = cx + scale / 3 - stroke;
    graphics_fill_rect(left, top, scale * 2 / 3, stroke, color);
    graphics_fill_rect(left, top, stroke, scale / 2 + stroke, color);
    graphics_fill_rect(left, mid, scale * 2 / 3, stroke, color);
    graphics_fill_rect(right, mid, stroke, scale / 2, color);
    graphics_fill_rect(left, cy + scale / 2 - stroke, scale * 2 / 3, stroke, color);
}

static void draw_bored_bg(void) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    graphics_fill_gradient_v(0, 0, (uint32_t)w, (uint32_t)h,
                             0xFF707CAF, 0xFF5C699F);

    fill_facet_triangle(0, 0, w * 13 / 100, 0, w * 14 / 100, h * 17 / 100, 0xFF202D80);
    fill_facet_triangle(0, 0, w * 14 / 100, h * 17 / 100, 0, h * 14 / 100, 0xFF35438F);
    fill_facet_triangle(w * 68 / 100, 0, w, 0, w, h * 40 / 100, 0xFF253381);
    fill_facet_triangle(w * 74 / 100, 0, w, h * 10 / 100, w, h * 40 / 100, 0xFF32428F);
    fill_facet_triangle(0, h * 23 / 100, w * 30 / 100, h * 69 / 100, 0, h * 92 / 100, 0xFF59679F);
    fill_facet_triangle(0, h * 92 / 100, w * 30 / 100, h * 69 / 100, w * 14 / 100, h, 0xFF1D2A78);
    fill_facet_triangle(w * 69 / 100, h * 82 / 100, w, h * 54 / 100, w * 91 / 100, h, 0xFF6573A7);
    fill_facet_triangle(w * 69 / 100, h * 82 / 100, w * 91 / 100, h, w * 64 / 100, h, 0xFF202E7A);

    int scale = h / 5;
    if (scale > 150) scale = 150;
    if (scale < 72) scale = 72;
    draw_solis_mark(w / 2 + 5, h / 2 + 6, scale, 0xFF101744);
    draw_solis_mark(w / 2, h / 2, scale, 0xFFFFFFFF);
    graphics_draw_string(w / 2 - 32, h / 2 + scale / 2 + 12, "SOLIS OS", 0xFFFFFFFF);
}

static void draw_aurora_bg(void) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    graphics_fill_gradient_v(0, 0, (uint32_t)w, (uint32_t)h, 0xFF122334, 0xFF151629);
    fill_facet_triangle(0, h * 72 / 100, w * 30 / 100, h * 34 / 100,
                        w * 58 / 100, h, 0xFF176B69);
    fill_facet_triangle(w * 16 / 100, h, w * 44 / 100, h * 38 / 100,
                        w * 82 / 100, h, 0xFF245787);
    fill_facet_triangle(w * 46 / 100, h, w * 73 / 100, h * 27 / 100,
                        w, h * 68 / 100, 0xFF4A3B72);
    for (int r = 130; r > 0; r -= 4)
        graphics_fill_circle(w * 3 / 5, h * 2 / 5, r,
                             lerp_color(0xFF58D6B0, 0xFF16283B, r, 130));
    graphics_fill_circle(w * 3 / 5, h * 2 / 5, 26, 0xFFC9F7E9);
}

static void draw_gnome_bg(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    graphics_fill_gradient_v(0, 0, w, h, 0xFF1E1E30, 0xFF0F0F17);

    struct { int x; int y; int r; uint32_t c; } blobs[] = {
        { (int)w * 3 / 8, (int)h * 3 / 8, 190, 0xFF3A5A86 },
        { (int)w * 7 / 8, (int)h * 2 / 3, 210, 0xFF5A4A8C },
        { (int)w / 8, (int)h * 3 / 4, 160, 0xFF2F6B55 },
    };
    for (int i = 0; i < 3; i++)
        for (int r = blobs[i].r; r > 0; r -= 4)
            graphics_fill_circle(blobs[i].x, blobs[i].y, r,
                lerp_color(blobs[i].c, 0xFF0F0F17, r, blobs[i].r));
}

static void draw_sunset_bg(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    uint32_t horizon = h * 3 / 5;

    struct { uint32_t y; uint32_t color; } stops[] = {
        {0,              0xFF0D0221},
        {horizon * 2/10, 0xFF6B1A4A},
        {horizon * 5/10, 0xFFC73E1A},
        {horizon * 8/10, 0xFFE8882A},
        {horizon,        0xFFFFD080},
    };
    for (uint32_t y = 0; y < horizon; y++) {
        int seg = 0;
        for (int i = 0; i < 4; i++)
            if (y >= stops[i].y && y < stops[i+1].y) { seg = i; break; }
        uint32_t t = y - stops[seg].y;
        uint32_t d = stops[seg+1].y - stops[seg].y;
        if (d == 0) d = 1;
        graphics_fill_rect(0, y, w, 1, lerp_color(stops[seg].color, stops[seg+1].color, t, d));
    }

    int sun_cx = w * 2 / 5;
    int sun_cy = horizon - 20;
    for (int r = 140; r >= 25; r -= 2) {
        uint32_t c = lerp_color(0xFFFFF8E0, 0xFFFF6020, r, 140);
        graphics_fill_circle(sun_cx, sun_cy, r, c);
    }
    graphics_fill_circle(sun_cx, sun_cy, 22, 0xFFFFF8E0);

    struct { int x; int y; int r; uint32_t c; } clouds[] = {
        {w*1/6, horizon*2/5, 35, 0xFFFFA060},
        {w*1/6+40, horizon*2/5-10, 30, 0xFFFFA060},
        {w*1/6+80, horizon*2/5+5, 25, 0xFFFFA060},
        {w*4/6, horizon*1/3, 40, 0xFFFF8850},
        {w*4/6+50, horizon*1/3-5, 35, 0xFFFF8850},
        {w*4/6+100, horizon*1/3+10, 30, 0xFFFF8850},
        {w*2/6, horizon*1/4, 30, 0xFFFFA070},
        {w*2/6+35, horizon*1/4-8, 28, 0xFFFFA070},
        {w*2/6+70, horizon*1/4+8, 25, 0xFFFFA070},
    };
    for (int i = 0; i < 9; i++)
        graphics_fill_circle(clouds[i].x, clouds[i].y, clouds[i].r, clouds[i].c);

    graphics_fill_rect(0, horizon, w, h - horizon, 0xFF1A0520);

    struct { int cx; int r; } mt[] = {
        {100, 140}, {280, 90}, {450, 160}, {650, 110}, {850, 80}, {(int)w-50, 180},
    };
    for (int i = 0; i < 6; i++)
        graphics_fill_circle(mt[i].cx, horizon + 80, mt[i].r, 0xFF2D1030);

    for (int x = 20; x < (int)w; x += 100 + (x * 7) % 60) {
        int th = 35 + (x * 13) % 50;
        int tw = 3 + (x * 3) % 4;
        graphics_fill_rect(x - tw/2, h - th, tw, th, 0xFF050208);
        graphics_fill_circle(x, h - th, 9 + (x * 5) % 7, 0xFF050208);
        graphics_fill_circle(x - 5, h - th + 4, 6 + (x * 7) % 4, 0xFF050208);
        graphics_fill_circle(x + 5, h - th + 4, 6 + (x * 3) % 4, 0xFF050208);
    }
}

static void draw_cherry_blossom_bg(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    uint32_t horizon = h * 55 / 100;

    for (uint32_t y = 0; y < horizon; y++)
        graphics_fill_rect(0, y, w, 1, lerp_color(0xFF87CEEB, 0xFFE8B8D0, y, horizon));

    graphics_fill_circle(w/4, horizon+30, 150, 0xFF8FA8A0);
    graphics_fill_circle(w*3/4, horizon+20, 130, 0xFF8FA8A0);
    graphics_fill_circle(w/2, horizon+40, 180, 0xFF8FA8A0);
    graphics_fill_rect(0, horizon, w, h - horizon, 0xFF6B9B5A);

    graphics_fill_circle(w/3, h-40, 200, 0xFF5E8D4A);
    graphics_fill_circle(w*2/3, h-20, 250, 0xFF6B9B5A);
    graphics_fill_circle(w/5, h-10, 180, 0xFF52803E);

    struct { int x; int y; int r; } cld[] = {
        {w/5, horizon/3, 30}, {w/5+40, horizon/3-8, 25}, {w/5+80, horizon/3+5, 22},
        {w*2/3, horizon/4, 35}, {w*2/3+50, horizon/4-5, 30},
    };
    for (int i = 0; i < 5; i++) {
        graphics_fill_circle(cld[i].x, cld[i].y, cld[i].r, 0xCCFFFFFF);
        graphics_fill_circle(cld[i].x-5, cld[i].y-3, cld[i].r*2/3, 0xFFFFFFFF);
    }

    int tx = w * 3 / 7;
    int ty = h - 20;

    graphics_fill_rect(tx-8, ty-80, 16, 80, 0xFF3D1F0A);
    graphics_fill_rect(tx-5, ty-100, 10, 20, 0xFF3D1F0A);
    graphics_fill_rect(tx-60, ty-120, 55, 6, 0xFF3D1F0A);
    graphics_fill_rect(tx+5, ty-110, 55, 6, 0xFF3D1F0A);
    graphics_fill_rect(tx-40, ty-140, 5, 25, 0xFF3D1F0A);
    graphics_fill_rect(tx-90, ty-115, 35, 4, 0xFF3D1F0A);
    graphics_fill_rect(tx+40, ty-130, 5, 25, 0xFF3D1F0A);
    graphics_fill_rect(tx+55, ty-105, 35, 4, 0xFF3D1F0A);
    graphics_fill_rect(tx-3, ty-150, 6, 15, 0xFF3D1F0A);
    graphics_fill_rect(tx-30, ty-155, 30, 4, 0xFF3D1F0A);
    graphics_fill_rect(tx, ty-145, 30, 4, 0xFF3D1F0A);

    struct { int x; int y; } tips[] = {
        {tx-90, ty-115}, {tx+90, ty-105}, {tx-40, ty-140}, {tx+40, ty-130},
        {tx-30, ty-155}, {tx+30, ty-145}, {tx-60, ty-120}, {tx+55, ty-110},
        {tx, ty-150}, {tx-20, ty-130}, {tx+20, ty-125}, {tx-50, ty-130}, {tx+50, ty-120},
    };
    uint32_t bc[] = {0xFFFFB7C5, 0xFFFF8FAB, 0xFFFF6B8F, 0xFFFFC0D0};

    for (int i = 0; i < 13; i++) {
        int bx = tips[i].x, by = tips[i].y;
        graphics_fill_circle(bx, by, 12+(i*3)%8, bc[i%4]);
        graphics_fill_circle(bx-8, by+4, 10+(i*5)%6, bc[(i+1)%4]);
        graphics_fill_circle(bx+8, by+2, 10+(i*7)%6, bc[(i+2)%4]);
        graphics_fill_circle(bx-4, by-6, 9+(i*2)%5, bc[(i+3)%4]);
        graphics_fill_circle(bx+5, by-5, 8+(i*3)%5, bc[(i+1)%4]);
    }

    uint32_t ps = 12345;
    for (int i = 0; i < 35; i++) {
        ps = ps * 1103515245 + 12345;
        int px = (ps >> 16) % w;
        ps = ps * 1103515245 + 12345;
        int py = (ps >> 16) % h;
        ps = ps * 1103515245 + 12345;
        uint32_t pc = bc[(ps >> 16) % 4];
        graphics_put_pixel(px, py, pc);
        if (px+1 < (int)w) graphics_put_pixel(px+1, py, pc);
        if (py+1 < (int)h) graphics_put_pixel(px, py+1, pc);
    }
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

static void draw_desktop_icons(void) {
    struct { int x; int y; } pos[7] = {
        {28, 60}, {116, 60}, {204, 60}, {292, 60},
        {28, 160}, {116, 160}, {204, 160},
    };
    int slots[7] = {0, 1, 2, 3, 4, 7, 8};
    for (int i = 0; i < 7; i++) {
        int slot = slots[i];
        draw_app_icon(pos[i].x, pos[i].y, 56, slot);
        int len = 0;
        for (const char *p = app_icons[slot].name; *p; p++) len++;
        graphics_draw_string(pos[i].x + (56 - len * 8) / 2, pos[i].y + 60, app_icons[slot].name, 0xFFC8C8D4);
    }
}

static void gui_draw_desktop(void) {
    if (bmp_is_wallpaper_loaded()) {
        bmp_blit_wallpaper();
    } else if (gui_theme == GUI_THEME_GNOME) {
        draw_gnome_bg();
    } else if (gui_theme == GUI_THEME_STARFIELD) {
        draw_starfield_bg();
    } else if (gui_theme == GUI_THEME_SUNSET) {
        draw_sunset_bg();
    } else if (gui_theme == GUI_THEME_CHERRY_BLOSSOM) {
        draw_cherry_blossom_bg();
    } else if (gui_theme == GUI_THEME_BORED) {
        draw_bored_bg();
    } else if (gui_theme == GUI_THEME_AURORA) {
        draw_aurora_bg();
    } else {
        graphics_fill_rect(0, 0, graphics_get_width(), graphics_get_height(), bg_color);
    }
    draw_desktop_icons();
}

/* bg_cache_usable means "the buffer holds a real desktop image that can be
 * blitted". bg_cache_valid doubles as the "captured this frame" flag, so a
 * stale or never-captured buffer must not be blitted. */
static void bg_cache_invalidate(void) {
    bg_cache_valid = false;
    bg_cache_usable = false;
}
static void bg_cache_capture(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    if (w > BG_CACHE_MAX_W || h > BG_CACHE_MAX_H) {
        bg_cache_invalidate();
        return;
    }
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++)
            bg_cache[y * w + x] = graphics_get_pixel(x, y);
    }
    bg_cache_w = w;
    bg_cache_h = h;
    bg_cache_valid = true;
    bg_cache_usable = true;
}


/* Blit only the part of the cached desktop that a damage rect covers. The
 * graphics clip is already narrowed to that rect, so an out-of-range x/y is
 * simply dropped by graphics_put_pixel. */
static void bg_cache_blit_region(int rx, int ry, int rw, int rh) {
    if (!bg_cache_valid) return;
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int)bg_cache_w) rw = (int)bg_cache_w - rx;
    if (ry + rh > (int)bg_cache_h) rh = (int)bg_cache_h - ry;
    for (int y = ry; y < ry + rh; y++)
        for (int x = rx; x < rx + rw; x++)
            graphics_put_pixel(x, y, bg_cache[(uint32_t)y * bg_cache_w + (uint32_t)x]);
}

/* ------------------------------------------------------------------ */
/* Clock / date formatting                                             */
/* ------------------------------------------------------------------ */
static void gui_format_time(char *buf, int size) {
    if (!buf || size < 6) return;
    struct rtc_time t;
    rtc_get_time(&t);
    int h = t.hour, m = t.minute;
    if (h < 0) h = 0;
    if (h > 23) h = 23;
    if (m < 0) m = 0;
    if (m > 59) m = 59;
    buf[0] = '0' + (char)(h / 10);
    buf[1] = '0' + (char)(h % 10);
    buf[2] = ':';
    buf[3] = '0' + (char)(m / 10);
    buf[4] = '0' + (char)(m % 10);
    buf[5] = '\0';
}

static void gui_format_date(char *buf, int size) {
    static const char *month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!buf || size < 12) return;
    struct rtc_time t;
    rtc_get_time(&t);
    int mon = t.month, day = t.day;
    if (mon < 1) mon = 1;
    if (mon > 12) mon = 12;
    if (day < 1) day = 1;
    if (day > 31) day = 31;
    buf[0] = month_names[mon - 1][0];
    buf[1] = month_names[mon - 1][1];
    buf[2] = month_names[mon - 1][2];
    buf[3] = ' ';
    buf[4] = '0' + (char)(day / 10);
    buf[5] = '0' + (char)(day % 10);
    buf[6] = '\0';
}

/* ------------------------------------------------------------------ */
/* Taskbar                                                             */
/*                                                                     */
/* A floating rounded card pinned to the bottom of the screen: the    */
/* launcher on the left, pinned app launchers plus one button per open */
/* window in the middle, and the system tray on the right.            */
/* ------------------------------------------------------------------ */
#define PIN_COUNT    9
#define PIN_W        30
#define PIN_GAP      2
#define PIN_ICON     22
#define MIN_TASK_W   70
#define MAX_TASK_W   150
#define TASK_GAP     4
#define LAUNCH_SZ    34
#define CARD_PAD     7
#define ITEM_RADIUS  7

static int task_wins[MAX_WINDOWS];
static int task_n = 0;
static int hover_pin = -1;

struct tb_layout {
    int card_x, card_y, card_w, card_h;
    int launch_x, launch_y, launch_sz;
    int tray_x, tray_w;
    int free_x, free_w;
    int pin_x, pin_n;
    int task_x, task_w, task_y, task_h;
};

static int tray_width(void) {
    return 168;
}

static void draw_text_right(int x, int y, const char *s, uint32_t c) {
    int len = 0;
    for (const char *p = s; *p; p++) len++;
    graphics_draw_string(x - len * 8, y, s, c);
}


static void gui_build_task_list(void) {
    task_n = 0;
    for (int zi = 0; zi < zcount && task_n < MAX_WINDOWS; zi++) {
        int i = zorder[zi];
        if (windows[i].visible) task_wins[task_n++] = i;
    }
}

static bool app_running(int app_id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].visible && windows[i].app_id == app_id) return true;
    }
    return false;
}

/* Single source of truth for the taskbar geometry, shared by drawing and
 * hit testing so the two can never disagree. */
static void taskbar_compute(struct tb_layout *L) {
    int sw = (int)graphics_get_width();
    int sh = (int)graphics_get_height();

    L->card_w = sw - TASKBAR_SIDE * 2;
    if (L->card_w < 160) L->card_w = sw;
    L->card_h = TASKBAR_H;
    L->card_x = (sw - L->card_w) / 2;
    L->card_y = sh - TASKBAR_MARGIN - L->card_h;
    if (L->card_y < 0) L->card_y = 0;

    L->launch_sz = LAUNCH_SZ;
    int max_sz = L->card_h - 2 * (CARD_PAD - 3);
    if (L->launch_sz > max_sz) L->launch_sz = max_sz;
    L->launch_x = L->card_x + CARD_PAD;
    L->launch_y = L->card_y + (L->card_h - L->launch_sz) / 2;

    L->tray_w = tray_width();
    L->tray_x = L->card_x + L->card_w - CARD_PAD - L->tray_w;

    L->free_x = L->launch_x + L->launch_sz + CARD_PAD + 2;
    L->free_w = L->tray_x - 4 - L->free_x;
    if (L->free_w < 0) L->free_w = 0;

    /* Pinned launchers only claim space the window buttons do not need, so
     * the taskbar degrades to plain window buttons on a narrow screen. */
    int need = task_n > 0
        ? task_n * MIN_TASK_W + (task_n - 1) * TASK_GAP
        : 0;
    int pins = PIN_COUNT;
    if (task_n > 0) {
        int spare = L->free_w - need - 12;
        pins = (spare > 0) ? spare / (PIN_W + PIN_GAP) : 0;
    }
    if (pins > PIN_COUNT) pins = PIN_COUNT;
    if (pins < 0) pins = 0;

    int pins_w = pins > 0 ? pins * (PIN_W + PIN_GAP) - PIN_GAP : 0;
    int sep = (pins > 0 && task_n > 0) ? 12 : 0;
    int task_area = L->free_w - pins_w - sep;

    int bw = 0;
    if (task_n > 0) {
        bw = (task_area - (task_n - 1) * TASK_GAP) / task_n;
        if (bw > MAX_TASK_W) bw = MAX_TASK_W;
        if (bw < MIN_TASK_W) bw = MIN_TASK_W;
    }
    L->task_w = bw;
    L->task_h = L->card_h - 2 * (CARD_PAD - 3);
    L->task_y = L->card_y + (L->card_h - L->task_h) / 2;

    int group = pins_w + sep
        + (task_n > 0 ? task_n * bw + (task_n - 1) * TASK_GAP : 0);
    int gx = L->free_x + (L->free_w - group) / 2;
    if (gx < L->free_x) gx = L->free_x;

    L->pin_x = gx;
    L->pin_n = pins;
    L->task_x = gx + pins_w + sep;
}

static int pin_at(int mx, int my) {
    struct tb_layout L;
    taskbar_compute(&L);
    if (L.pin_n <= 0) return -1;
    if (my < L.task_y - 2 || my > L.task_y + L.task_h + 2) return -1;
    for (int i = 0; i < L.pin_n; i++) {
        int x = L.pin_x + i * (PIN_W + PIN_GAP);
        if (mx >= x - 1 && mx < x + PIN_W) return i;
    }
    return -1;
}

static int task_at(int mx, int my) {
    struct tb_layout L;
    gui_build_task_list();
    taskbar_compute(&L);
    if (task_n == 0 || L.task_w <= 0) return -1;
    if (my < L.task_y - 2 || my > L.task_y + L.task_h + 2) return -1;
    for (int i = 0; i < task_n; i++) {
        int x = L.task_x + i * (L.task_w + TASK_GAP);
        if (mx >= x && mx < x + L.task_w) return task_wins[i];
    }
    return -1;
}

static void gui_draw_tray(int tx, int ty, int tw, int th) {
    int y = ty + (th - 16) / 2;
    int right = tx + tw;

    char timebuf[8];
    gui_format_time(timebuf, 8);
    draw_text_right(right, y, timebuf, C_TEXT);
    right -= 46;

    char datebuf[12];
    gui_format_date(datebuf, 12);
    draw_text_right(right, y, datebuf, C_TEXT_DIM);
    right -= 42;

    graphics_fill_rect(right, ty + 8, 1, th - 16, C_PANEL_LINE);
    right -= 12;

    char rambuf[12];
    uint32_t mb = sys_get_total_ram();
    rambuf[0] = 'R'; rambuf[1] = 'A'; rambuf[2] = 'M'; rambuf[3] = ' ';
    char tmp[8];
    int ti = 0;
    if (mb == 0) tmp[ti++] = '0';
    while (mb > 0) { tmp[ti++] = '0' + (mb % 10); mb /= 10; }
    int bi = 4;
    while (ti > 0) rambuf[bi++] = tmp[--ti];
    rambuf[bi++] = 'M';
    rambuf[bi++] = '\0';
    draw_text_right(right, y, rambuf, C_TEXT_DIM);
}

/* Soft shadow cast by the taskbar card onto the desktop above it. */
static void gui_draw_taskbar_shadow(const struct tb_layout *L) {
    int fb_w = (int)graphics_get_width();
    int fb_h = (int)graphics_get_height();
    for (int off = 1; off <= 5; off++) {
        int alpha = 16 - off * 3;
        if (alpha <= 0) continue;
        int sy = L->card_y - off;
        if (sy < 0 || sy >= fb_h) continue;
        for (int xx = L->card_x + TASKBAR_RADIUS; xx < L->card_x + L->card_w - TASKBAR_RADIUS; xx++) {
            if (xx < 0 || xx >= fb_w) continue;
            graphics_put_pixel(xx, sy,
                blend_pixel(graphics_get_pixel(xx, sy), 0xFF000000, alpha));
        }
    }
}

static void gui_draw_launcher(const struct tb_layout *L) {
    int x = L->launch_x, y = L->launch_y, s = L->launch_sz;
    uint32_t bg = start_menu_open ? C_ACTIVE
                : (hover_logo ? C_BTN_HOVER : C_BTN);
    uint32_t br = start_menu_open ? C_ACTIVE_DK : C_PANEL_LINE;
    graphics_fill_rounded_rect(x, y, s, s, ITEM_RADIUS + 1, bg);
    graphics_draw_rounded_rect(x, y, s, s, ITEM_RADIUS + 1, br);

    int gx = x + s / 2 - 9, gy = y + s / 2 - 5;
    graphics_fill_rounded_rect(gx, gy, 4, 4, 1, C_TEXT);
    graphics_fill_rounded_rect(gx + 7, gy, 4, 4, 1, C_ACTIVE);
    graphics_fill_rounded_rect(gx, gy + 7, 4, 4, 1, C_TEXT_DIM);
    graphics_fill_rounded_rect(gx + 7, gy + 7, 4, 4, 1, C_ACTIVE);
}

static void gui_draw_pin(const struct tb_layout *L, int slot, int x) {
    int y = L->task_y;
    int h = L->task_h;
    int ix = x + (PIN_W - PIN_ICON) / 2;
    int iy = y + (h - PIN_ICON) / 2;

    if (slot == hover_pin) {
        graphics_fill_rounded_rect(x - 1, y, PIN_W + 2, h, ITEM_RADIUS, C_BTN_HOVER);
    }
    draw_app_icon(ix, iy, PIN_ICON, slot);

    if (app_running(slot + 1)) {
        graphics_fill_rounded_rect(x + PIN_W / 2 - 5, y + h - 3, 10, 2, 1, C_ACTIVE);
    }
}

static void gui_draw_task_button(const struct tb_layout *L, int idx, int x) {
    struct gui_window *w = &windows[idx];
    int y = L->task_y, bw = L->task_w, bh = L->task_h;
    bool hov = (idx == hover_task);
    bool focused = w->focused;

    uint32_t bg = focused ? 0xFF23384E : (hov ? C_BTN_HOVER : C_BTN);
    uint32_t br = focused ? C_ACTIVE : C_PANEL_LINE;
    graphics_fill_rounded_rect(x, y, bw, bh, ITEM_RADIUS, bg);
    graphics_draw_rounded_rect(x, y, bw, bh, ITEM_RADIUS, br);

    int slot = w->app_id - 1;
    if (slot >= 0 && slot < 9) {
        int cy = y + (bh - 14) / 2;
        draw_app_icon(x + 6, cy, 14, slot);
    }

    uint32_t tc = w->minimized ? C_TEXT_DIM : C_TEXT;
    int maxc = (bw - 32) / 8;
    int tl = 0;
    while (w->title[tl] && tl < maxc) tl++;
    char label[24];
    for (int i = 0; i < tl; i++) label[i] = w->title[i];
    label[tl] = '\0';
    graphics_draw_string(x + 26, y + (bh - 16) / 2, label, tc);

    if (focused) {
        graphics_fill_rounded_rect(x + bw / 2 - 8, y + bh - 4, 16, 2, 1, C_ACTIVE);
    } else if (!w->minimized) {
        graphics_fill_rounded_rect(x + bw / 2 - 5, y + bh - 4, 10, 2, 1, C_PANEL_LINE);
    }
}

static void gui_draw_panel(void) {
    struct tb_layout L;
    gui_build_task_list();
    taskbar_compute(&L);

    gui_draw_taskbar_shadow(&L);

    /* Card: rounded, with a 1px top highlight for a little depth. */
    graphics_fill_rounded_rect(L.card_x, L.card_y, L.card_w, L.card_h,
                               TASKBAR_RADIUS, C_PANEL);
    graphics_fill_rect(L.card_x + TASKBAR_RADIUS, L.card_y + 1,
                       L.card_w - TASKBAR_RADIUS * 2, 1, 0xFF1B2B3E);
    graphics_draw_rounded_rect(L.card_x, L.card_y, L.card_w, L.card_h,
                               TASKBAR_RADIUS, C_PANEL_LINE);

    gui_draw_launcher(&L);

    for (int i = 0; i < L.pin_n; i++)
        gui_draw_pin(&L, i, L.pin_x + i * (PIN_W + PIN_GAP));

    if (L.pin_n > 0 && task_n > 0) {
        int sep_x = L.pin_x + L.pin_n * (PIN_W + PIN_GAP) - PIN_GAP + 5;
        graphics_fill_rect(sep_x, L.card_y + 10, 1, L.card_h - 20, C_PANEL_LINE);
    }

    for (int i = 0; i < task_n; i++)
        gui_draw_task_button(&L, task_wins[i], L.task_x + i * (L.task_w + TASK_GAP));

    gui_draw_tray(L.tray_x, L.card_y, L.tray_w, L.card_h);
}

/* ------------------------------------------------------------------ */
/* Start menu                                                          */
/* ------------------------------------------------------------------ */
struct menu_item {
    int kind;       /* 0 = app, 1 = session, 2 = section header */
    int arg;
    const char *label;
};

static const struct menu_item menu_items[] = {
    {2, 0, "Applications"},
    {0, 0, "Helios"},
    {0, 1, "Notes"},
    {0, 2, "Paint"},
    {0, 4, "Files"},
    {0, 7, "Editor"},
    {2, 0, "Utilities"},
    {0, 5, "Task Manager"},
    {0, 6, "Package Manager"},
    {0, 8, "Tetris"},
    {2, 0, "Settings"},
    {0, 3, "Settings"},
    {2, 0, "Session"},
    {1, 1, "Shutdown"},
    {1, 2, "Restart"},
    {1, 3, "Logout"},
};
#define MENU_NUM ((int)(sizeof(menu_items) / sizeof(menu_items[0])))

static int menu_height(void) {
    int h = MENU_HEADER_H;
    for (int i = 0; i < MENU_NUM; i++)
        h += (menu_items[i].kind == 2) ? MENU_SEC_H : MENU_ITEM_H;
    return h;
}

static void menu_geom(int *x, int *y, int *w, int *h) {
    struct tb_layout L;
    taskbar_compute(&L);
    *x = L.card_x + CARD_PAD;
    *w = MENU_W;
    *h = menu_height();
    *y = L.card_y - *h - 6;
    if (*y < 0) *y = 0;
}

static int menu_item_at(int mx, int my) {
    int x, y, w, h;
    menu_geom(&x, &y, &w, &h);
    if (mx < x || mx >= x + w || my < y || my >= y + h) return -1;
    int iy = y + MENU_HEADER_H;
    for (int i = 0; i < MENU_NUM; i++) {
        if (menu_items[i].kind == 2) { iy += MENU_SEC_H; continue; }
        if (my >= iy && my < iy + MENU_ITEM_H) return i;
        iy += MENU_ITEM_H;
    }
    return -1;
}

static void session_glyph(int kind, int x, int y, uint32_t c) {
    int cx = x + 12, cy = y + 12;
    if (kind == 1) {                       /* shutdown: power icon */
        graphics_draw_circle(cx, cy, 4, c);
        graphics_fill_rect(cx - 1, cy - 7, 2, 4, c);
    } else if (kind == 2) {                /* restart: circular arrow */
        graphics_draw_circle(cx, cy, 4, c);
        graphics_fill_rect(cx - 1, cy - 6, 2, 3, c);
        graphics_put_pixel(cx + 3, cy - 4, c);
        graphics_put_pixel(cx + 4, cy - 3, c);
        graphics_put_pixel(cx + 4, cy - 2, c);
    } else if (kind == 3) {                /* logout: arrow out of box */
        graphics_draw_rect(cx - 5, cy - 4, 4, 8, c);
        draw_line(cx - 1, cy - 4, cx + 3, cy, c);
        draw_line(cx - 1, cy + 4, cx + 3, cy, c);
        draw_line(cx + 3, cy, cx + 1, cy - 2, c);
        draw_line(cx + 3, cy, cx + 1, cy + 2, c);
    }
}

static void gui_draw_start_menu(void) {
    int x, y, w, h;
    menu_geom(&x, &y, &w, &h);

    /* brand header */
    graphics_fill_rect(x, y, w, MENU_HEADER_H, C_ACTIVE);
    graphics_draw_string(x + 8, y + (MENU_HEADER_H - 16) / 2, "Solis OS", 0xFFFFFFFF);

    /* body */
    graphics_fill_rect(x, y + MENU_HEADER_H, w, h - MENU_HEADER_H, C_PANEL);
    graphics_draw_rect(x, y, w, h, C_PANEL_LINE);

    int iy = y + MENU_HEADER_H;
    for (int i = 0; i < MENU_NUM; i++) {
        const struct menu_item *mi = &menu_items[i];
        if (mi->kind == 2) {
            graphics_draw_string(x + 10, iy + 2, mi->label, C_TEXT_DIM);
            graphics_fill_rect(x + 10, iy + MENU_SEC_H - 3, w - 20, 1, C_PANEL_LINE);
            iy += MENU_SEC_H;
            continue;
        }
        bool hov = (i == menu_hover);
        if (hov) {
            graphics_fill_rect(x + 1, iy, w - 2, MENU_ITEM_H, C_ACTIVE);
        }
        if (mi->kind == 0) {
            int slot = mi->arg;
            draw_app_icon(x + 8, iy + (MENU_ITEM_H - 14) / 2, 14, slot);
        } else {
            session_glyph(mi->arg, x + 8, iy + (MENU_ITEM_H - 24) / 2, hov ? 0xFFFFFFFF : C_TEXT_DIM);
        }
        graphics_draw_string(x + 30, iy + (MENU_ITEM_H - 16) / 2, mi->label,
                             hov ? 0xFFFFFFFF : C_TEXT);
        if (hov)
            graphics_draw_rect(x + 1, iy, w - 2, MENU_ITEM_H, C_ACTIVE_DK);
        iy += MENU_ITEM_H;
    }
}

static void menu_activate(int item) {
    if (item < 0 || item >= MENU_NUM) return;
    const struct menu_item *mi = &menu_items[item];
    if (mi->kind == 0) {
        gui_launch_app(mi->arg);
    } else if (mi->kind == 1) {
        if (mi->arg == 1) {              /* shutdown */
            outw(0x604, 0x2000);         /* QEMU ACPI power off */
            for (;;) __asm__ volatile("cli; hlt");
        } else if (mi->arg == 2) {       /* restart */
            outb(0x64, 0xFE);
            for (;;) __asm__ volatile("cli; hlt");
        } else if (mi->arg == 3) {       /* logout */
            logout_requested = true;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Window management API                                               */
/* ------------------------------------------------------------------ */
static int gui_alloc_window(const char *title, int app_id) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        if (!windows[i].visible) {
            struct gui_window *w = &windows[i];
            w->visible = true;
            w->focused = true;
            w->minimized = false;
            w->maximized = false;
            w->dragging = false;
            w->resizing = false;
            w->resize_flags = 0;
            w->bounds.x = 60 + (app_id * 30);
            w->bounds.y = 40 + (app_id * 24);
            w->bounds.width = 500;
            w->bounds.height = 360;
            w->restore = w->bounds;
            w->app_id = app_id;
            int j = 0;
            while (title[j] && j < 23) {
                w->title[j] = title[j];
                j++;
            }
            w->title[j] = '\0';
            gui_focus_window(i);
            gui_damage_window(i);
            gui_damage_taskbar();
            return i;
        }
    }
    return -1;
}

int gui_launch_terminal(void)      { return gui_alloc_window("Helios", 1); }
int gui_launch_notes(void)         { return gui_alloc_window("Notes", 2); }
int gui_launch_paint(void)         { return gui_alloc_window("Paint", 3); }
int gui_launch_settings(void)      { return gui_alloc_window("Settings", 4); }
int gui_launch_filebrowser(void)   { return gui_alloc_window("Files", 5); }
int gui_launch_taskmanager(void)   { return gui_alloc_window("Task Manager", 6); }
int gui_launch_pkg(void)           { return gui_alloc_window("SOLPKG", 7); }
int gui_launch_editor(void)        { return gui_alloc_window("Editor", 8); }
int gui_launch_tetris(void)        { return gui_alloc_window("Tetris", 9); }

int gui_launch_app(int slot) {
    switch (slot) {
        case 0: return gui_launch_terminal();
        case 1: return gui_launch_notes();
        case 2: return gui_launch_paint();
        case 3: return gui_launch_settings();
        case 4: return gui_launch_filebrowser();
        case 5: return gui_launch_taskmanager();
        case 6: return gui_launch_pkg();
        case 7: return gui_launch_editor();
        case 8: return gui_launch_tetris();
        default: return -1;
    }
}

int gui_open_with(int slot, const char *path) {
    if (!path || slot < 0 || slot >= SPX_MAX_APPS) return -1;
    int idx = gui_launch_app(slot);
    if (idx < 0) return -1;
    const struct app_exports *app = spx_get_exports(slot);
    if (app && app->open_file) app->open_file(path);
    gui_damage_window(idx);
    gui_damage_taskbar();
    return idx;
}

static void gui_minimize_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    /* Capture the old geometry first: once minimized the bounds are gone and
     * the taskbar entry takes over. */
    struct gui_rect old = windows[idx].bounds;
    windows[idx].minimized = true;
    windows[idx].focused = false;
    windows[idx].dragging = false;
    windows[idx].resizing = false;
    drag_window = -1;
    z_remove(idx);
    gui_focus_top();
    gui_damage(old.x, old.y, old.width + WIN_SHADOW_EXTENT,
               old.height + WIN_SHADOW_EXTENT);
    gui_damage_taskbar();
}

static void gui_restore_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    windows[idx].minimized = false;
    gui_focus_window(idx);
    gui_damage_window(idx);
    gui_damage_taskbar();
}

static void gui_toggle_maximize(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    struct gui_window *w = &windows[idx];
    /* Both the old and the new geometry can cover very different areas, and
     * a maximized window leaves a large gap to refill. */
    struct gui_rect old = w->bounds;
    if (w->maximized) {
        w->bounds = w->restore;
        w->maximized = false;
    } else {
        w->restore = w->bounds;
        w->bounds.x = 0;
        w->bounds.y = 0;
        w->bounds.width = graphics_get_width();
        w->bounds.height = graphics_get_height() - PANEL_H;
        w->maximized = true;
    }
    gui_damage(old.x, old.y, old.width + WIN_SHADOW_EXTENT,
               old.height + WIN_SHADOW_EXTENT);
    gui_damage_window(idx);
}

void gui_close_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    struct gui_rect old = windows[idx].bounds;
    windows[idx].visible = false;
    windows[idx].focused = false;
    windows[idx].dragging = false;
    windows[idx].resizing = false;
    if (drag_window == idx) drag_window = -1;
    z_remove(idx);
    gui_focus_top();
    gui_damage(old.x, old.y, old.width + WIN_SHADOW_EXTENT,
               old.height + WIN_SHADOW_EXTENT);
    gui_damage_taskbar();
}

void gui_init(void) {
    for (int i = 0; i < MAX_WINDOWS; ++i) {
        struct gui_window *w = &windows[i];
        w->visible = false;
        w->focused = false;
        w->minimized = false;
        w->maximized = false;
        w->dragging = false;
        w->resizing = false;
        w->resize_flags = 0;
        w->bounds.x = 0;
        w->bounds.y = 0;
        w->bounds.width = 500;
        w->bounds.height = 360;
        w->title[0] = '\0';
        w->app_id = 0;
    }
    zcount = 0;
    active_window = -1;
    drag_window = -1;
    gui_mouse_prev_right = false;
    start_menu_open = false;
    menu_hover = -1;
    hover_logo = 0;
    hover_task = -1;
    prev_cursor_x = -1;
    prev_cursor_y = -1;
    damage_n = 0;
    damage_all = true;
    redraw_pending = true;
    bg_cache_invalidate();
    bmp_cache_wallpaper("/desktop/wallpaper.bmp");
}

void gui_set_bg_color(uint32_t color) { bg_color = color; gui_theme = GUI_THEME_SOLID; bg_cache_invalidate(); gui_damage_all(); }
uint32_t gui_get_bg_color(void) { return bg_color; }
void gui_set_theme(int theme) { gui_theme = theme; bg_cache_invalidate(); gui_damage_all(); }
int gui_get_theme(void) { return gui_theme; }

/* ------------------------------------------------------------------ */
/* Save dialog (blocking modal)                                        */
/* ------------------------------------------------------------------ */
#define DIALOG_W      420
#define DIALOG_H      280
#define DIALOG_PLACES 5
#define DIALOG_NAME_MAX 23

static bool save_dialog_active = false;
static bool open_dialog_mode = false;
static int  save_result = 0;
static char dialog_save_dir[64];
static char dialog_save_name[DIALOG_NAME_MAX + 1];
static int  dialog_name_len = 0;
static int  dialog_hover_btn = 0;
static int  dialog_hover_place = -1;
static char dialog_files[SOLFS_MAX_FILES][SOLFS_MAX_NAME];
static int dialog_file_count = 0;
static int dialog_file_selected = -1;
static int dialog_file_scroll = 0;

static void dialog_refresh_files(void) {
    dialog_file_count = vfs_ls_at(dialog_save_dir, dialog_files, SOLFS_MAX_FILES);
    if (dialog_file_selected >= dialog_file_count) dialog_file_selected = -1;
    dialog_file_scroll = 0;
}

static const struct { const char *label; const char *path; } dialog_places[DIALOG_PLACES] = {
    {"C:/",        "/"},
    {"Home",       "/home"},
    {"Documents",  "/docs"},
    {"Downloads",  "/downloads"},
    {"Desktop",    "/desktop"},
};

static void gui_str_cpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int gui_str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static bool gui_str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int dialog_place_at(int mx, int my) {
    int x = ((int)graphics_get_width() - DIALOG_W) / 2;
    int y = ((int)graphics_get_height() - DIALOG_H) / 2;
    for (int i = 0; i < DIALOG_PLACES; i++) {
        int iy = y + 56 + i * 24;
        if (mx >= x + 12 && mx < x + 162 && my >= iy && my < iy + 24)
            return i;
    }
    return -1;
}

static int dialog_file_at(int mx, int my) {
    if (!open_dialog_mode) return -1;
    int x = ((int)graphics_get_width() - DIALOG_W) / 2;
    int y = ((int)graphics_get_height() - DIALOG_H) / 2;
    int list_x = x + 174;
    int list_y = y + 56;
    int by = y + DIALOG_H - 32;
    if (mx < list_x || mx >= x + DIALOG_W - 12 || my < list_y || my >= by - 6)
        return -1;
    int idx = dialog_file_scroll + (my - list_y) / 24;
    return idx < dialog_file_count ? idx : -1;
}

static int dialog_btn_at(int mx, int my) {
    int x = ((int)graphics_get_width() - DIALOG_W) / 2;
    int y = ((int)graphics_get_height() - DIALOG_H) / 2;
    int by = y + DIALOG_H - 32;
    if (mx >= x + DIALOG_W - 142 && mx < x + DIALOG_W - 82 && my >= by && my < by + 22)
        return 1;
    if (mx >= x + DIALOG_W - 76 && mx < x + DIALOG_W - 12 && my >= by && my < by + 22)
        return 2;
    return 0;
}

static void gui_draw_save_dialog(void) {
    int x = ((int)graphics_get_width() - DIALOG_W) / 2;
    int y = ((int)graphics_get_height() - DIALOG_H) / 2;
    int w = DIALOG_W, h = DIALOG_H;

    gui_draw_window_shadow(x, y, w, h);
    graphics_fill_rect(x, y, w, h, C_WIN_BG);
    graphics_draw_rect(x, y, w, h, C_ACTIVE_DK);
    graphics_fill_rect(x, y, w, TITLEBAR_H, C_ACTIVE);
    graphics_draw_string(x + 8, y + (TITLEBAR_H - 16) / 2,
                         open_dialog_mode ? "Open file" : "Save file", 0xFFFFFFFF);

    graphics_draw_string(x + 12, y + 32, "Save in:", C_TEXT_DIM);
    graphics_draw_string(x + 74, y + 32, dialog_save_dir, C_TEXT);

    for (int i = 0; i < DIALOG_PLACES; i++) {
        int iy = y + 56 + i * 24;
        bool sel = gui_str_eq(dialog_save_dir, dialog_places[i].path);
        bool hov = i == dialog_hover_place;
        uint32_t bg = sel ? C_ACTIVE : (hov ? C_BTN_HOVER : C_BTN);
        uint32_t fg = sel ? 0xFFFFFFFF : C_TEXT;
        graphics_fill_rect(x + 12, iy, 150, 22, bg);
        graphics_draw_rect(x + 12, iy, 150, 22, C_PANEL_LINE);
        graphics_draw_string(x + 20, iy + 4, dialog_places[i].label, fg);
    }

    if (open_dialog_mode) {
        int list_x = x + 174, list_y = y + 56;
        int list_w = w - 186, list_h = h - 94;
        graphics_fill_rect(list_x, list_y, list_w, list_h, 0xFF101216);
        graphics_draw_rect(list_x, list_y, list_w, list_h, C_PANEL_LINE);
        int visible = (list_h - 4) / 24;
        for (int i = 0; i < visible; i++) {
            int idx = dialog_file_scroll + i;
            if (idx >= dialog_file_count) break;
            int iy = list_y + 2 + i * 24;
            bool selected = idx == dialog_file_selected;
            uint32_t bg = selected ? C_ACTIVE : C_PANEL;
            uint32_t fg = selected ? 0xFFFFFFFF : C_TEXT;
            graphics_fill_rect(list_x + 2, iy, list_w - 4, 22, bg);
            graphics_draw_string(list_x + 8, iy + 3, dialog_files[idx], fg);
        }
        if (dialog_file_count == 0)
            graphics_draw_string(list_x + 8, list_y + 8, "This folder is empty", C_TEXT_DIM);
    } else {
        graphics_draw_string(x + 12, y + 154, "Name:", C_TEXT_DIM);
        graphics_fill_rect(x + 60, y + 150, w - 72, 22, 0xFF101216);
        graphics_draw_rect(x + 60, y + 150, w - 72, 22, C_ACTIVE_DK);
        graphics_draw_string(x + 66, y + 154, dialog_save_name, C_TEXT);
        graphics_fill_rect(x + 66 + dialog_name_len * 8, y + 155, 1, 14, C_TEXT);
    }

    int by = y + h - 32;
    uint32_t sbg = dialog_hover_btn == 1 ? C_BTN_HOVER : 0xFF3B8B3B;
    graphics_fill_rect(x + w - 142, by, 60, 22, sbg);
    graphics_draw_rect(x + w - 142, by, 60, 22, C_PANEL_LINE);
    graphics_draw_string(x + w - 142 + (open_dialog_mode ? 12 : 17), by + 4,
                         open_dialog_mode ? "Open" : "Save", 0xFFFFFFFF);

    uint32_t cbg = dialog_hover_btn == 2 ? C_BTN_HOVER : C_BTN;
    graphics_fill_rect(x + w - 76, by, 64, 22, cbg);
    graphics_draw_rect(x + w - 76, by, 64, 22, C_PANEL_LINE);
    graphics_draw_string(x + w - 76 + 18, by + 4, "Cancel", 0xFFFFFFFF);
}

static int gui_file_dialog(bool open_mode, const char *suggested, char *out_path, int out_max) {
    if (!out_path || out_max <= 0) return 0;
    open_dialog_mode = open_mode;
    dialog_name_len = 0;
    if (suggested) {
        for (int i = 0; suggested[i] && dialog_name_len < DIALOG_NAME_MAX; i++) {
            if (suggested[i] == '/') continue;
            dialog_save_name[dialog_name_len++] = suggested[i];
        }
    }
    dialog_save_name[dialog_name_len] = '\0';

    const char *cwd = vfs_get_cwd();
    gui_str_cpy(dialog_save_dir, (cwd && cwd[0]) ? cwd : "/home",
                (int)sizeof(dialog_save_dir));

    dialog_file_selected = -1;
    dialog_file_scroll = 0;
    if (open_dialog_mode) dialog_refresh_files();

    save_dialog_active = true;
    save_result = 0;
    dialog_hover_btn = 0;
    dialog_hover_place = -1;

    struct pointer_state ps;
    bool dlg_prev_left = false;
    if (pointer_poll(&ps))
        dlg_prev_left = (ps.buttons & 0x01) != 0;

    /* The dialog is modal, so open it over a freshly drawn desktop. */
    save_dialog_active = true;
    gui_damage_all();
    gui_redraw();

    while (save_result == 0) {
        if (pointer_poll(&ps)) {
            bool changed = false;

            if (ps.dx || ps.dy) {
                gui_damage_cursor(mouse_x, mouse_y);
                mouse_x += ps.dx;
                mouse_y += ps.dy;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
                if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;
                gui_damage_cursor(mouse_x, mouse_y);
            }

            bool left = (ps.buttons & 0x01) != 0;
            if (left && !dlg_prev_left) {
                int place = dialog_place_at(mouse_x, mouse_y);
                int btn = dialog_btn_at(mouse_x, mouse_y);
                int file = dialog_file_at(mouse_x, mouse_y);
                if (place >= 0) {
                    gui_str_cpy(dialog_save_dir, dialog_places[place].path,
                                (int)sizeof(dialog_save_dir));
                    dialog_file_selected = -1;
                    if (open_dialog_mode) dialog_refresh_files();
                } else if (open_dialog_mode && file >= 0) {
                    dialog_file_selected = file;
                } else if (btn == 1 && open_dialog_mode && dialog_file_selected >= 0) {
                    const char *name = dialog_files[dialog_file_selected];
                    int len = gui_str_len(name);
                    if (len > 0 && name[len - 1] == '/') {
                        int dir_len = gui_str_len(dialog_save_dir);
                        if (dir_len > 0 && dialog_save_dir[dir_len - 1] == '/') dir_len--;
                        if (dir_len + len + 1 < (int)sizeof(dialog_save_dir)) {
                            dialog_save_dir[dir_len++] = '/';
                            for (int i = 0; i < len - 1; i++)
                                dialog_save_dir[dir_len++] = name[i];
                            dialog_save_dir[dir_len] = '\0';
                            dialog_file_selected = -1;
                            dialog_refresh_files();
                        }
                    } else {
                        save_result = 1;
                    }
                } else if (btn == 1 && !open_dialog_mode && dialog_name_len > 0) {
                    save_result = 1;
                } else if (btn == 2) {
                    save_result = -1;
                }
                changed = true;
            }
            dlg_prev_left = left;

            int hb = dialog_btn_at(mouse_x, mouse_y);
            int hp = dialog_place_at(mouse_x, mouse_y);
            if (hb != dialog_hover_btn || hp != dialog_hover_place) {
                dialog_hover_btn = hb;
                dialog_hover_place = hp;
                changed = true;
            }

            if (changed) gui_damage_all();
        }

        if (keyboard_has_input()) {
            char k = 0;
            if (keyboard_read_char(&k)) {
                if (!open_dialog_mode && (k == '\n' || k == '\r')) {
                    if (dialog_name_len > 0) save_result = 1;
                } else if (k == 0x1b) {
                    save_result = -1;
                } else if (open_dialog_mode && (k == 'j' || k == 'k')) {
                    if (dialog_file_count > 0) {
                        if (dialog_file_selected < 0) dialog_file_selected = 0;
                        else if (k == 'j' && dialog_file_selected + 1 < dialog_file_count) dialog_file_selected++;
                        else if (k == 'k' && dialog_file_selected > 0) dialog_file_selected--;
                        int visible = (DIALOG_H - 94 - 4) / 24;
                        if (dialog_file_selected < dialog_file_scroll)
                            dialog_file_scroll = dialog_file_selected;
                        if (dialog_file_selected >= dialog_file_scroll + visible)
                            dialog_file_scroll = dialog_file_selected - visible + 1;
                    }
                    gui_damage_all();
                } else if (open_dialog_mode && (k == '\n' || k == '\r')) {
                    if (dialog_file_selected >= 0) {
                        const char *name = dialog_files[dialog_file_selected];
                        int len = gui_str_len(name);
                        if (len > 0 && name[len - 1] == '/') {
                            int dir_len = gui_str_len(dialog_save_dir);
                            if (dir_len > 0 && dialog_save_dir[dir_len - 1] == '/') dir_len--;
                            if (dir_len + len + 1 < (int)sizeof(dialog_save_dir)) {
                                dialog_save_dir[dir_len++] = '/';
                                for (int i = 0; i < len - 1; i++)
                                    dialog_save_dir[dir_len++] = name[i];
                                dialog_save_dir[dir_len] = '\0';
                                dialog_file_selected = -1;
                                dialog_refresh_files();
                            }
                        } else save_result = 1;
                    }
                    gui_damage_all();
                } else if (k == '\b') {
                    if (!open_dialog_mode && dialog_name_len > 0)
                        dialog_save_name[--dialog_name_len] = '\0';
                    gui_damage_all();
                } else if (!open_dialog_mode && k >= 32 && dialog_name_len < DIALOG_NAME_MAX && k != '/') {
                    dialog_save_name[dialog_name_len++] = k;
                    dialog_save_name[dialog_name_len] = '\0';
                    gui_damage_all();
                }
            }
        }

        if (redraw_pending) gui_redraw();
        __asm__ volatile("hlt");
    }

    int r = (save_result == 1) ? 1 : 0;

    if (r == 1 && open_dialog_mode && dialog_file_selected >= 0) {
        int di = 0;
        int dl = gui_str_len(dialog_save_dir);
        for (int i = 0; i < dl && di < out_max - 1; i++) out_path[di++] = dialog_save_dir[i];
        if (di > 0 && out_path[di - 1] == '/') {
            if (di > 1) di--;
        } else if (di < out_max - 1) {
            out_path[di++] = '/';
        }
        const char *name = dialog_files[dialog_file_selected];
        for (int i = 0; name[i] && name[i] != '/' && di < out_max - 1; i++) out_path[di++] = name[i];
        out_path[di] = '\0';
    } else if (r == 1) {
        int di = 0;
        int dl = gui_str_len(dialog_save_dir);
        for (int i = 0; i < dl && di < out_max - 1; i++)
            out_path[di++] = dialog_save_dir[i];
        if (dl > 1 && di < out_max - 1)
            out_path[di++] = '/';
        for (int i = 0; dialog_save_name[i] && di < out_max - 1; i++)
            out_path[di++] = dialog_save_name[i];
        out_path[di] = '\0';
    } else if (out_max > 0) {
        out_path[0] = '\0';
    }

    save_dialog_active = false;
    dialog_hover_btn = 0;
    dialog_hover_place = -1;
    gui_mouse_prev_left = dlg_prev_left;

    /* Defer the redraw to the main loop so it runs after the caller writes
     * the file; otherwise an open Files window shows a stale listing. */
    redraw_pending = true;

    return r;
}

int gui_save_dialog(const char *suggested, char *out_path, int out_max) {
    return gui_file_dialog(false, suggested, out_path, out_max);
}

int gui_open_dialog(char *out_path, int out_max) {
    return gui_file_dialog(true, 0, out_path, out_max);
}

/* ------------------------------------------------------------------ */
/* Damage helpers for individual layers                                */
/* ------------------------------------------------------------------ */
static void gui_damage_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    const struct gui_rect *r = &windows[idx].bounds;
    /* The shadow only bleeds right and down, so no pad is needed on the
     * left or top edges. */
    gui_damage(r->x, r->y, r->width + WIN_SHADOW_EXTENT,
               r->height + WIN_SHADOW_EXTENT);
}

static void gui_damage_active_content(void) {
    if (active_window < 0 || active_window >= MAX_WINDOWS) return;
    const struct gui_rect *r = &windows[active_window].bounds;
    /* Only the client area changes when an app handles input. */
    gui_damage(r->x + WIN_BORDER, r->y + TITLEBAR_H,
               r->width - 2 * WIN_BORDER, r->height - TITLEBAR_H - WIN_BORDER);
}

static void gui_damage_taskbar(void) {
    struct tb_layout L;
    taskbar_compute(&L);
    gui_damage_pad(L.card_x, L.card_y, L.card_w, L.card_h, 4);
}

/* The clock only rewrites the right-aligned tray text, so the damage can stay
 * inside the tray. The card's rounded corners are at the far ends, well
 * outside this rect, and every layer still repaints within it. */
static void gui_damage_tray(void) {
    struct tb_layout L;
    taskbar_compute(&L);
    gui_damage(L.tray_x, L.card_y + 2, L.tray_w + 8, L.card_h - 4);
}

static void gui_damage_menu(void) {
    int x, y, w, h;
    menu_geom(&x, &y, &w, &h);
    gui_damage(x, y, w, h);
}

/* The cursor is a layer of its own: damage both where it was and where it is
 * going, then draw it once on top after the other layers are settled. */
static void gui_damage_cursor(int x, int y) {
    if (x < 0) return;
    gui_damage(x - 1, y - 1, CURSOR_SIZE + 2, CURSOR_SIZE + 2);
}

static bool rect_overlaps(int ax, int ay, int aw, int ah,
                          int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* ------------------------------------------------------------------ */
/* Redraw                                                              */
/* ------------------------------------------------------------------ */

/* Repaint one screen rect: cached desktop, then every window bottom-up in
 * z-order, then the taskbar and any overlay. The clip is set to the rect so
 * layers outside it cost nothing. */
static void gui_paint_region(int rx, int ry, int rw, int rh) {
    if (rw <= 0 || rh <= 0) return;

    graphics_push_clip((uint32_t)rx, (uint32_t)ry, (uint32_t)rw, (uint32_t)rh);
    graphics_begin_frame();

    if (bg_cache_usable) {
        bg_cache_blit_region(rx, ry, rw, rh);
    } else {
        /* Cache miss: the desktop has to be rendered in full, and the cache
         * captured from it, so this pass cannot be clipped to the damage
         * rect. Only happens on the first paint and after a theme change. */
        graphics_pop_clip();
        gui_draw_desktop();
        bg_cache_capture();
        graphics_push_clip((uint32_t)rx, (uint32_t)ry, (uint32_t)rw, (uint32_t)rh);
    }

    /* windows bottom-up by z-order */
    for (int zi = zcount - 1; zi >= 0; zi--) {
        int i = zorder[zi];
        const struct gui_rect *b = &windows[i].bounds;
        if (!windows[i].visible || windows[i].minimized) continue;
        if (!rect_overlaps(rx, ry, rw, rh, b->x, b->y,
                           b->width + WIN_SHADOW_EXTENT,
                           b->height + WIN_SHADOW_EXTENT))
            continue;
        gui_draw_window(&windows[i]);
        gui_draw_window_content(&windows[i]);
    }

    {
        int sw = (int)graphics_get_width();
        if (rect_overlaps(rx, ry, rw, rh, 0, sw - PANEL_H, sw, PANEL_H))
            gui_draw_panel();
    }
    if (start_menu_open) {
        int mx, my, mw, mh;
        menu_geom(&mx, &my, &mw, &mh);
        if (rect_overlaps(rx, ry, rw, rh, mx, my, mw, mh))
            gui_draw_start_menu();
    }
    if (save_dialog_active) gui_draw_save_dialog();

    graphics_end_frame();
    graphics_pop_clip();
}

void gui_redraw(void) {
    if (prev_cursor_x >= 0 &&
        (prev_cursor_x != mouse_x || prev_cursor_y != mouse_y))
        gui_damage_cursor(prev_cursor_x, prev_cursor_y);
    gui_damage_cursor(mouse_x, mouse_y);
    graphics_reset_pixel_counter();

    /* Without a usable desktop cache the first region painted has to render
     * the whole desktop anyway, so drive it as a full-screen pass. */
    if (!bg_cache_usable) {
        paint_rects = 1;
        paint_was_all = 1;
        gui_paint_region(0, 0, (int)graphics_get_width(), (int)graphics_get_height());
    } else if (damage_all) {
        paint_rects = 1;
        paint_was_all = 1;
        gui_paint_region(0, 0, (int)graphics_get_width(), (int)graphics_get_height());
    } else {
        paint_rects = damage_n;
        paint_was_all = 0;
        last_paint_n = damage_n;
        for (int i = 0; i < damage_n && i < 8; i++) last_paint[i] = damage[i];
        for (int i = 0; i < damage_n; i++)
            gui_paint_region(damage[i].x, damage[i].y,
                             damage[i].width, damage[i].height);
    }
    damage_n = 0;
    damage_all = false;

    /* The cursor lives above every other layer, so it is drawn last and its
     * old pixels were already repaired by the damage pass above. */
    graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
    prev_cursor_x = mouse_x;
    prev_cursor_y = mouse_y;
    redraw_pending = false;

    /* Incremental rendering is only useful if the savings are real, so the
     * cost of each repaint goes to the serial log. A full-screen pass is
     * width*height writes; anything far below that means damage tracking
     * is doing its job. */
    uint32_t wrote = graphics_pixel_writes();
    uint32_t total = graphics_get_width() * graphics_get_height();
    last_paint_writes = wrote;
    if (GUI_REDRAW_TRACE && total) {
        console_write("[GFX] paint ");
        console_write_dec(wrote);
        console_write(" px (");
        console_write_dec(total ? (wrote * 100) / total : 0);
        console_write("% of screen), rects=");
        console_write_dec(paint_rects);
        console_write(", all=");
        console_write_dec(paint_was_all);
        console_write("\n");
    }
    if (GUI_REDRAW_TRACE_DUMP && paint_rects <= 8) {
        for (int i = 0; i < last_paint_n; i++) {
            console_write("   [GFX] rect ");
            console_write_dec(last_paint[i].x); console_write(",");
            console_write_dec(last_paint[i].y); console_write(" ");
            console_write_dec(last_paint[i].width); console_write("x");
            console_write_dec(last_paint[i].height);
            console_write("\n");
        }
    }
}

bool gui_needs_redraw(void) { return redraw_pending; }

void gui_toggle_start_menu(void) {
    start_menu_open = !start_menu_open;
    menu_hover = -1;
    /* The menu anchors to the launcher button, so both halves repaint. */
    gui_damage_menu();
    gui_damage_taskbar();
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static bool pt_in_rect(int px, int py, const struct gui_rect *r) {
    return px >= r->x && px < r->x + r->width && py >= r->y && py < r->y + r->height;
}

void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons) {
    bool left_down = buttons & 0x01;
    bool left_click = left_down && !gui_mouse_prev_left;
    bool left_release = !left_down && gui_mouse_prev_left;
    bool right_down = buttons & 0x02;
    bool right_click = right_down && !gui_mouse_prev_right;
    gui_mouse_prev_left = left_down;
    gui_mouse_prev_right = right_down;

    int sw = graphics_get_width();
    int sh = graphics_get_height();
    int py = sh - PANEL_H;

    /* update pointer position: damage where the cursor was and where it is
     * going, and let the repaint draw it in the right z-order. */
    if (dx || dy) {
        gui_damage_cursor(mouse_x, mouse_y);
        mouse_x += dx; mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= sw) mouse_x = sw - 1;
        if (mouse_y >= sh) mouse_y = sh - 1;
        gui_damage_cursor(mouse_x, mouse_y);
    }

    /* hover tracking: start menu */
    if (start_menu_open) {
        int hov = menu_item_at(mouse_x, mouse_y);
        if (hov != menu_hover) { menu_hover = hov; gui_damage_menu(); }
    }
    /* hover tracking: taskbar */
    if (mouse_y >= py) {
        struct tb_layout L;
        taskbar_compute(&L);
        int new_logo = (mouse_x >= L.launch_x && mouse_x < L.launch_x + L.launch_sz
                        && mouse_y >= L.launch_y
                        && mouse_y < L.launch_y + L.launch_sz) ? 1 : 0;
        int new_pin = pin_at(mouse_x, mouse_y);
        int new_task = task_at(mouse_x, mouse_y);
        if (new_logo != hover_logo) { hover_logo = new_logo; gui_damage_taskbar(); }
        if (new_pin != hover_pin) { hover_pin = new_pin; gui_damage_taskbar(); }
        if (new_task != hover_task) { hover_task = new_task; gui_damage_taskbar(); }
    } else {
        /* Compare against -1 explicitly: hover_task and hover_pin use -1 for
         * "nothing hovered", and -1 is truthy as a C int. Treating it as
         * active repainted the whole taskbar on every mouse move. */
        if (hover_logo || hover_task >= 0 || hover_pin >= 0) {
            hover_logo = 0;
            hover_task = -1;
            hover_pin = -1;
            gui_damage_taskbar();
        }
    }

    if (right_click && drag_window < 0) {
        for (int zi = 0; zi < zcount; zi++) {
            int i = zorder[zi];
            struct gui_window *w = &windows[i];
            if (!w->visible || w->minimized || !pt_in_rect(mouse_x, mouse_y, &w->bounds)) continue;
            gui_focus_window(i);
            const struct app_exports *app = spx_get_exports(w->app_id - 1);
            if (app && app->handle_context) {
                int cx = w->bounds.x + WIN_BORDER;
                int cy = w->bounds.y + TITLEBAR_H;
                int cw = w->bounds.width - 2 * WIN_BORDER;
                int ch = w->bounds.height - TITLEBAR_H - WIN_BORDER;
                app->handle_context(cx, cy, cw, ch, mouse_x, mouse_y);
                gui_damage(cx, cy, cw, ch);
            }
            mouse_buttons = buttons;
            return;
        }
    }

    /* continue dragging / resizing */
    if (left_down && drag_window >= 0) {
        struct gui_window *w = &windows[drag_window];
        struct gui_rect old = w->bounds;
        if (w->dragging) {
            int nx = mouse_x - w->drag_off_x;
            int ny = mouse_y - w->drag_off_y;
            if (nx < SNAP_DIST) nx = 0;
            else if (nx + w->bounds.width > sw - SNAP_DIST) nx = sw - w->bounds.width;
            if (ny < SNAP_DIST) ny = 0;
            else if (ny + w->bounds.height > sh - PANEL_H - SNAP_DIST)
                ny = sh - PANEL_H - w->bounds.height;
            if (ny < 0) ny = 0;
            if (ny > sh - PANEL_H - 40) ny = sh - PANEL_H - 40;
            if (nx != w->bounds.x || ny != w->bounds.y) {
                w->bounds.x = nx;
                w->bounds.y = ny;
                gui_damage_union(&old, &w->bounds);
            }
        } else if (w->resizing) {
            int nw = w->resize_ow + (mouse_x - w->resize_ox);
            int nh = w->resize_oh + (mouse_y - w->resize_oy);
            if (nw < MIN_WIN_W) nw = MIN_WIN_W;
            if (nh < MIN_WIN_H) nh = MIN_WIN_H;
            if (!(w->resize_flags & 1)) nw = w->resize_ow;
            if (!(w->resize_flags & 2)) nh = w->resize_oh;
            w->bounds.width = nw;
            w->bounds.height = nh;
            gui_damage_union(&old, &w->bounds);
        }
        mouse_buttons = buttons;
        return;
    }

    /* release ends drag / resize */
    if (left_release && drag_window >= 0) {
        int idx = drag_window;
        windows[idx].dragging = false;
        windows[idx].resizing = false;
        drag_window = -1;
        /* The drop may have snapped the window to a new position. */
        gui_damage_window(idx);
        mouse_buttons = buttons;
        return;
    }

    if (!left_click) {
        mouse_buttons = buttons;
        return;
    }

    /* ---- left click ---- */
    if (start_menu_open) {
        int mx, my, mw, mh;
        menu_geom(&mx, &my, &mw, &mh);
        if (mouse_x >= mx && mouse_x < mx + mw && mouse_y >= my && mouse_y < my + mh) {
            int item = menu_item_at(mouse_x, mouse_y);
            if (item >= 0) menu_activate(item);
            start_menu_open = false;
            /* The menu disappears and whatever it covered has to come back. */
            gui_damage_menu();
            mouse_buttons = buttons;
            return;
        }
        start_menu_open = false;
        gui_damage_menu();
    }

    /* windows, topmost first */
    for (int zi = 0; zi < zcount; zi++) {
        int i = zorder[zi];
        struct gui_window *w = &windows[i];
        if (!w->visible || w->minimized) continue;
        struct gui_rect r = w->bounds;
        if (!pt_in_rect(mouse_x, mouse_y, &r)) continue;

        int bx = r.x + r.width - 3 * BTN_W - 1;

        /* control buttons in titlebar */
        if (mouse_y < r.y + TITLEBAR_H) {
            if (mouse_x >= bx + 2 * BTN_W && mouse_x < bx + 3 * BTN_W) {
                gui_close_window(i);
                mouse_buttons = buttons;
                return;
            }
            if (mouse_x >= bx + BTN_W && mouse_x < bx + 2 * BTN_W) {
                gui_toggle_maximize(i);
                mouse_buttons = buttons;
                return;
            }
            if (mouse_x >= bx && mouse_x < bx + BTN_W) {
                gui_minimize_window(i);
                mouse_buttons = buttons;
                return;
            }
        }

        gui_focus_window(i);

        if (mouse_y < r.y + TITLEBAR_H) {
            if (!w->maximized) {
                w->dragging = true;
                w->resizing = false;
                drag_window = i;
                w->drag_off_x = mouse_x - r.x;
                w->drag_off_y = mouse_y - r.y;
            }
        } else {
            int flags = 0;
            if (mouse_x >= r.x + r.width - RESIZE_MARGIN) flags |= 1;
            if (mouse_y >= r.y + r.height - RESIZE_MARGIN) flags |= 2;
            if (flags && !w->maximized) {
                w->dragging = false;
                w->resizing = true;
                w->resize_flags = flags;
                drag_window = i;
                w->resize_ox = mouse_x;
                w->resize_oy = mouse_y;
                w->resize_ow = r.width;
                w->resize_oh = r.height;
            } else {
                const struct app_exports *app = spx_get_exports(w->app_id - 1);
                if (app && app->handle_mouse) {
                    int cx = r.x + WIN_BORDER;
                    int cy = r.y + TITLEBAR_H;
                    int cw = r.width - 2 * WIN_BORDER;
                    int ch = r.height - TITLEBAR_H - WIN_BORDER;
                    app->handle_mouse(cx, cy, cw, ch, mouse_x, mouse_y);
                }
            }
        }
        mouse_buttons = buttons;
        return;
    }

    /* taskbar */
    if (mouse_y >= py) {
        struct tb_layout L;
        taskbar_compute(&L);
        if (mouse_x >= L.launch_x && mouse_x < L.launch_x + L.launch_sz
            && mouse_y >= L.launch_y && mouse_y < L.launch_y + L.launch_sz) {
            gui_toggle_start_menu();
        } else {
            int pin = pin_at(mouse_x, mouse_y);
            if (pin >= 0) {
                gui_launch_app(pin);
            } else {
                int idx = task_at(mouse_x, mouse_y);
                if (idx >= 0) {
                    if (windows[idx].minimized) {
                        gui_restore_window(idx);
                    } else if (windows[idx].focused) {
                        gui_minimize_window(idx);
                    } else {
                        gui_focus_window(idx);
                    }
                }
            }
        }
        mouse_buttons = buttons;
        return;
    }

    /* desktop icons */
    struct { int x; int y; } pos[7] = {
        {28, 60}, {116, 60}, {204, 60}, {292, 60},
        {28, 160}, {116, 160}, {204, 160},
    };
    int slots[7] = {0, 1, 2, 3, 4, 7, 8};
    for (int i = 0; i < 7; i++) {
        struct gui_rect ir = {pos[i].x, pos[i].y, 56, 56};
        if (pt_in_rect(mouse_x, mouse_y, &ir)) {
            gui_launch_app(slots[i]);
            break;
        }
    }

    mouse_buttons = buttons;
}

void gui_handle_key(char key) {
    if (key == 0x1b) {
        if (start_menu_open) {
            start_menu_open = false;
            gui_damage_menu();
            return;
        }
    }
    if (key == '\t' && keyboard_is_alt_pressed()) {
        gui_cycle_focus();
        return;
    }
    if (keyboard_is_ctrl_pressed() && keyboard_is_alt_pressed() &&
        (key == 't' || key == 'T')) {
        gui_launch_terminal();
        return;
    }
    if (active_window < 0 || active_window >= MAX_WINDOWS) return;
    if (!windows[active_window].visible || windows[active_window].minimized) return;
    int slot = windows[active_window].app_id - 1;
    const struct app_exports *app = spx_get_exports(slot);
    if (app && app->handle_key) app->handle_key(key);
    /* Apps only ever redraw their own client area. */
    gui_damage_active_content();
}

void gui_handle_scroll(int notches) {
    if (notches == 0) return;

    /* Wheel events go to the topmost window under the pointer, and to the app
     * only when the pointer is really over its client area. Menus and
     * taskbars are handled by the desktop itself. */
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        struct gui_window *w = &windows[i];
        if (!w->visible || w->minimized) continue;
        struct gui_rect r = w->bounds;
        if (mouse_x < r.x || mouse_x >= r.x + r.width ||
            mouse_y < r.y || mouse_y >= r.y + r.height) continue;

        int cx = r.x + WIN_BORDER;
        int cy = r.y + TITLEBAR_H;
        int cw = r.width - 2 * WIN_BORDER;
        int ch = r.height - TITLEBAR_H - WIN_BORDER;
        if (mouse_x < cx || mouse_x >= cx + cw ||
            mouse_y < cy || mouse_y >= cy + ch) continue;

        const struct app_exports *app = spx_get_exports(w->app_id - 1);
        if (app && app->handle_scroll) {
            app->handle_scroll(cx, cy, cw, ch, notches);
            gui_damage(cx, cy, cw, ch);
        }
        return;
    }
}

void gui_update_clock(void) {
    if (redraw_pending) return;
    /* Only the tray text changes, so keep the damage inside the tray. */
    gui_damage_tray();
    gui_damage_cursor(mouse_x, mouse_y);
    gui_redraw();
}

bool gui_take_logout(void) {
    bool r = logout_requested;
    logout_requested = false;
    return r;
}

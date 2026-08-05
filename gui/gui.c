/*
 * Solis OS desktop environment - BoredOS-inspired lightweight shell.
 *
 * Direct framebuffer rendering. Flat rectangles, 1px borders, bitmap
 * fonts, square monochrome control glyphs. No transparency, no shadows,
 * no rounded corners, no animation. Stacking window manager.
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
#define PANEL_H       28
#define WIN_BORDER    1
#define BTN_W         22          /* window control button width  */
#define RESIZE_MARGIN 5
#define MIN_WIN_W     240
#define MIN_WIN_H     140
#define LOGO_W        42
#define MENU_W        200
#define MENU_ITEM_H   24
#define MENU_HEADER_H 30
#define MENU_SEC_H    18
#define SNAP_DIST     8

/* ------------------------------------------------------------------ */
/* Palette (BoredOS-inspired, ~14 colors)                              */
/* ------------------------------------------------------------------ */
#define C_BG          0xFF2C2F33  /* desktop background (dark slate) */
#define C_PANEL       0xFF24272B  /* panel / menu background         */
#define C_PANEL_LINE  0xFF1B1E22  /* panel bevel / border            */
#define C_ACTIVE      0xFF4C7BD9  /* industrial blue (accent)        */
#define C_ACTIVE_DK   0xFF3A63B3  /* accent, pressed/darker          */
#define C_INACTIVE    0xFF3B4048  /* inactive window titlebar        */
#define C_INACTIVE_DK 0xFF2E3238  /* inactive border                 */
#define C_WIN_BG      0xFF202225  /* window body                     */
#define C_TEXT        0xFFEAEAEA  /* primary text                    */
#define C_TEXT_DIM    0xFF888888  /* secondary text / disabled       */
#define C_BTN         0xFF3B4048  /* flat button fill                */
#define C_BTN_HOVER   0xFF474D56  /* flat button hover               */
#define C_BTN_PRESS   0xFF2F3338  /* flat button pressed             */
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
static int active_window = -1;
static int drag_window = -1;
static bool redraw_pending = true;

static bool start_menu_open = false;
static int  menu_hover = -1;
static int  hover_logo = 0;
static int  hover_task = -1;

static bool logout_requested = false;

static int gui_theme = GUI_THEME_SOLID;
static uint32_t bg_color = C_BG;

/* The procedural desktop backdrops are expensive, so they (plus the desktop
 * icons) are rendered once into a cache and blitted on every redraw. */
#define BG_CACHE_MAX_W 1024
#define BG_CACHE_MAX_H 768
static uint32_t bg_cache[BG_CACHE_MAX_W * BG_CACHE_MAX_H];
static uint32_t bg_cache_w = 0;
static uint32_t bg_cache_h = 0;
static bool bg_cache_valid = false;

#define CURSOR_SIZE 16
static uint32_t cursor_bg[CURSOR_SIZE * CURSOR_SIZE];
static int prev_cursor_x = -1;
static int prev_cursor_y = -1;

/* ------------------------------------------------------------------ */
/* Cursor helpers                                                      */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* App icon table (monochrome-ish glyphs for menu / taskbar)           */
/* ------------------------------------------------------------------ */
static const struct {
    const char *name;
    char glyph;
    uint32_t color;
} app_icons[9] = {
    {"Terminal",      '>', 0xFF5CB85C},
    {"Notes",         'N', 0xFF4C7BD9},
    {"Paint",         'P', 0xFFE0576B},
    {"Settings",      'S', 0xFFD9A441},
    {"Files",         'F', 0xFF3FA06A},
    {"Task Manager",  'T', 0xFFB055E0},
    {"SOLPKG",         'P', 0xFF46A8E8},
    {"Editor",        'E', 0xFF35B79B},
    {"Tetris",        'T', 0xFFE0863C},
};

static void draw_app_glyph(int x, int y, int size, int slot) {
    if (slot < 0 || slot > 8) return;
    graphics_fill_rect(x, y, size, size, app_icons[slot].color);
    graphics_draw_rect(x, y, size, size, 0xFF1B1E22);
    char glyph[2] = {app_icons[slot].glyph, '\0'};
    graphics_draw_string(x + (size - 8) / 2, y + (size - 16) / 2, glyph, 0xFF000000);
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
    active_window = idx;
    for (int k = 0; k < MAX_WINDOWS; k++)
        windows[k].focused = (k == idx);
    z_raise(idx);
    redraw_pending = true;
}

static void gui_focus_top(void) {
    for (int zi = 0; zi < zcount; zi++) {
        int i = zorder[zi];
        if (windows[i].visible && !windows[i].minimized) {
            gui_focus_window(i);
            return;
        }
    }
    active_window = -1;
    for (int k = 0; k < MAX_WINDOWS; k++) windows[k].focused = false;
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

static void gui_draw_ctrl_btn(int x, int y, bool hover, bool press,
                              void (*glyph)(int, int, uint32_t), uint32_t glyph_color) {
    uint32_t bg = press ? C_BTN_PRESS : (hover ? C_BTN_HOVER : C_BTN);
    graphics_fill_rect(x, y, BTN_W, TITLEBAR_H, bg);
    graphics_draw_rect(x, y, BTN_W, TITLEBAR_H, 0xFF1B1E22);
    glyph(x, y, glyph_color);
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
    const int extent = 4;
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
    uint32_t border = w->focused ? C_ACTIVE_DK : C_INACTIVE_DK;
    uint32_t tcol   = w->focused ? 0xFFFFFFFF : 0xFFB4B8BF;

    /* soft shadow under the window */
    gui_draw_window_shadow(x, y, ww, hh);

    /* body */
    graphics_fill_rect(x, y, ww, hh, C_WIN_BG);
    /* titlebar (flat, sharp corners) */
    graphics_fill_rect(x, y, ww, TITLEBAR_H, bar);
    /* 1px border all around */
    graphics_draw_rect(x, y, ww, hh, border);

    /* left-aligned title */
    graphics_draw_string(x + 6, y + (TITLEBAR_H - 16) / 2, w->title, tcol);

    /* control buttons: [min] [max] [close] at top-right */
    int bx = x + ww - 3 * BTN_W - 1;
    int by = y;
    bool top = zcount > 0 && w == &windows[zorder[0]];
    bool over = top && !w->dragging && !w->resizing
                && mouse_y >= by && mouse_y < by + TITLEBAR_H
                && mouse_x >= bx && mouse_x < bx + 3 * BTN_W;
    bool left = (mouse_buttons & 0x01) != 0;
    bool h_min   = over && mouse_x <  bx + BTN_W;
    bool h_max   = over && mouse_x >= bx + BTN_W && mouse_x < bx + 2 * BTN_W;
    bool h_close = over && mouse_x >= bx + 2 * BTN_W;
    gui_draw_ctrl_btn(bx,                 by, h_min,   h_min   && left, glyph_min, tcol);
    gui_draw_ctrl_btn(bx + BTN_W,         by, h_max,   h_max   && left,
                      w->maximized ? glyph_restore : glyph_max, tcol);
    gui_draw_ctrl_btn(bx + 2 * BTN_W,     by, h_close, h_close && left, glyph_close, tcol);
}

static void gui_draw_window_content(struct gui_window *w) {
    if (!w->visible || w->minimized) return;
    int x = w->bounds.x, y = w->bounds.y;
    int ww = w->bounds.width, hh = w->bounds.height;
    int cx = x + WIN_BORDER;
    int cy = y + TITLEBAR_H;
    int cw = ww - 2 * WIN_BORDER;
    int ch = hh - TITLEBAR_H - WIN_BORDER;
    graphics_fill_rect(cx, cy, cw, ch, C_WIN_BG);
    const struct app_exports *app = spx_get_exports(w->app_id - 1);
    if (app && app->draw) app->draw(cx, cy, cw, ch);
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
        draw_app_glyph(pos[i].x, pos[i].y, 56, slot);
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
    } else {
        graphics_fill_rect(0, 0, graphics_get_width(), graphics_get_height(), bg_color);
    }
    draw_desktop_icons();
}

static void bg_cache_invalidate(void) {
    bg_cache_valid = false;
}

static void bg_cache_capture(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    if (w > BG_CACHE_MAX_W || h > BG_CACHE_MAX_H) {
        bg_cache_valid = false;
        return;
    }
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++)
            bg_cache[y * w + x] = graphics_get_pixel(x, y);
    }
    bg_cache_w = w;
    bg_cache_h = h;
    bg_cache_valid = true;
}

static void bg_cache_blit(void) {
    if (bg_cache_valid)
        graphics_blit_rgb(0, 0, bg_cache_w, bg_cache_h, bg_cache_w, bg_cache);
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
/* Bottom panel                                                        */
/* ------------------------------------------------------------------ */
static int tray_width(void) {
    return 172;
}

static void draw_text_right(int x, int y, const char *s, uint32_t c) {
    int len = 0;
    for (const char *p = s; *p; p++) len++;
    graphics_draw_string(x - len * 8, y, s, c);
}

static void gui_draw_tray(void) {
    int sw = graphics_get_width();
    int sh = graphics_get_height();
    int py = sh - PANEL_H;
    int cx = sw - 8;                 /* right margin */
    int y = py + (PANEL_H - 16) / 2;

    char timebuf[8];
    gui_format_time(timebuf, 8);
    draw_text_right(cx, y, timebuf, C_TEXT);
    cx -= 48;

    char datebuf[12];
    gui_format_date(datebuf, 12);
    draw_text_right(cx, y, datebuf, C_TEXT_DIM);
    cx -= 40;

    /* separator */
    graphics_fill_rect(cx - 6, py + 7, 1, PANEL_H - 14, C_PANEL_LINE);
    cx -= 14;

    /* memory indicator */
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
    draw_text_right(cx, y, rambuf, C_TEXT_DIM);
}

/* Task buttons */
static int task_wins[MAX_WINDOWS];
static int task_n = 0;

static void gui_build_task_list(void) {
    task_n = 0;
    for (int zi = 0; zi < zcount && task_n < MAX_WINDOWS; zi++) {
        int i = zorder[zi];
        if (windows[i].visible) task_wins[task_n++] = i;
    }
}

static void task_geometry(int *start_x, int *btn_w, int *gap) {
    int sw = graphics_get_width();
    int avail = sw - LOGO_W - 16 - tray_width() - 24;
    int n = task_n;
    if (n == 0) { *start_x = 0; *btn_w = 0; *gap = 4; return; }
    int bw = (avail - (n - 1) * 4) / n;
    if (bw > 160) bw = 160;
    if (bw < 80) bw = 80;
    *btn_w = bw;
    *gap = 4;
    int total = n * bw + (n - 1) * *gap;
    *start_x = LOGO_W + 8 + (sw - LOGO_W - 8 - tray_width() - 8 - total) / 2;
    if (*start_x < LOGO_W + 8) *start_x = LOGO_W + 8;
}

static int task_at(int mx, int my) {
    int py = graphics_get_height() - PANEL_H;
    if (my < py || my >= py + PANEL_H) return -1;
    int sx, bw, gap;
    task_geometry(&sx, &bw, &gap);
    for (int i = 0; i < task_n; i++) {
        if (mx >= sx && mx < sx + bw) return task_wins[i];
        sx += bw + gap;
    }
    return -1;
}

static void gui_draw_task_button(int idx, int x, int bw) {
    struct gui_window *w = &windows[idx];
    int py = graphics_get_height() - PANEL_H;
    int y = py + 3;
    int h = PANEL_H - 6;
    bool hov = (idx == hover_task);
    uint32_t bg = w->focused ? C_ACTIVE : (hov ? C_BTN_HOVER : C_BTN);
    uint32_t br = w->focused ? C_ACTIVE_DK : C_PANEL_LINE;
    uint32_t tc = w->focused ? 0xFFFFFFFF
                 : (w->minimized ? C_TEXT_DIM : C_TEXT);
    graphics_fill_rect(x, y, bw, h, bg);
    graphics_draw_rect(x, y, bw, h, br);

    /* running indicator dot for focused app */
    if (w->focused) {
        graphics_fill_rect(x + 3, y + h / 2 - 1, 3, 3, 0xFFFFFFFF);
    } else if (!w->minimized) {
        graphics_fill_rect(x + 3, y + h / 2 - 1, 3, 3, C_TEXT_DIM);
    }

    int tx = x + 10;
    int tl = 0;
    const char *t = w->title;
    for (const char *p = t; *p; p++) tl++;
    int maxc = (bw - 14) / 8;
    if (tl > maxc) tl = maxc;
    char label[24];
    for (int i = 0; i < tl; i++) label[i] = t[i];
    label[tl] = '\0';
    graphics_draw_string(tx, y + (h - 16) / 2, label, tc);
}

static void gui_draw_panel(void) {
    int sw = graphics_get_width();
    int sh = graphics_get_height();
    int py = sh - PANEL_H;

    graphics_fill_rect(0, py, sw, PANEL_H, C_PANEL);
    graphics_fill_rect(0, py, sw, 1, C_PANEL_LINE);
    graphics_fill_rect(0, py + 1, sw, 1, 0xFF2E3238);

    /* logo / launcher button */
    int lx = 4, ly = py + 3, lw = LOGO_W, lh = PANEL_H - 6;
    uint32_t lbg = hover_logo ? (start_menu_open ? C_ACTIVE : C_BTN_HOVER) : C_BTN;
    graphics_fill_rect(lx, ly, lw, lh, lbg);
    graphics_draw_rect(lx, ly, lw, lh, start_menu_open ? C_ACTIVE_DK : C_PANEL_LINE);
    /* small pixel grid logo */
    int gx = lx + lw / 2 - 9, gy = ly + lh / 2 - 5;
    graphics_fill_rect(gx, gy, 4, 4, C_TEXT);
    graphics_fill_rect(gx + 7, gy, 4, 4, C_TEXT);
    graphics_fill_rect(gx, gy + 7, 4, 4, C_TEXT);
    graphics_fill_rect(gx + 7, gy + 7, 4, 4, C_TEXT);

    /* task buttons */
    gui_build_task_list();
    int sx, bw, gap;
    task_geometry(&sx, &bw, &gap);
    for (int i = 0; i < task_n; i++) {
        gui_draw_task_button(task_wins[i], sx, bw);
        sx += bw + gap;
    }

    gui_draw_tray();
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
    {0, 0, "Terminal"},
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
    *x = 4;
    *w = MENU_W;
    *h = menu_height();
    *y = graphics_get_height() - PANEL_H - *h;
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
            draw_app_glyph(x + 8, iy + (MENU_ITEM_H - 14) / 2, 14, slot);
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
            redraw_pending = true;
            return i;
        }
    }
    return -1;
}

int gui_launch_terminal(void)      { return gui_alloc_window("Terminal", 1); }
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

static void gui_minimize_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    windows[idx].minimized = true;
    windows[idx].focused = false;
    windows[idx].dragging = false;
    windows[idx].resizing = false;
    drag_window = -1;
    z_remove(idx);
    gui_focus_top();
    redraw_pending = true;
}

static void gui_restore_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    windows[idx].minimized = false;
    gui_focus_window(idx);
    redraw_pending = true;
}

static void gui_toggle_maximize(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    struct gui_window *w = &windows[idx];
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
    redraw_pending = true;
}

void gui_close_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS || !windows[idx].visible) return;
    windows[idx].visible = false;
    windows[idx].focused = false;
    windows[idx].dragging = false;
    windows[idx].resizing = false;
    if (drag_window == idx) drag_window = -1;
    z_remove(idx);
    gui_focus_top();
    redraw_pending = true;
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
    start_menu_open = false;
    menu_hover = -1;
    hover_logo = 0;
    hover_task = -1;
    prev_cursor_x = -1;
    prev_cursor_y = -1;
    redraw_pending = true;
    bg_cache_invalidate();
    bmp_cache_wallpaper("/desktop/wallpaper.bmp");
}

void gui_set_bg_color(uint32_t color) { bg_color = color; gui_theme = GUI_THEME_SOLID; bg_cache_invalidate(); redraw_pending = true; }
uint32_t gui_get_bg_color(void) { return bg_color; }
void gui_set_theme(int theme) { gui_theme = theme; bg_cache_invalidate(); redraw_pending = true; }
int gui_get_theme(void) { return gui_theme; }

/* ------------------------------------------------------------------ */
/* Save dialog (blocking modal)                                        */
/* ------------------------------------------------------------------ */
#define DIALOG_W      420
#define DIALOG_H      230
#define DIALOG_PLACES 5
#define DIALOG_NAME_MAX 23

static bool save_dialog_active = false;
static int  save_result = 0;
static char dialog_save_dir[64];
static char dialog_save_name[DIALOG_NAME_MAX + 1];
static int  dialog_name_len = 0;
static int  dialog_hover_btn = 0;
static int  dialog_hover_place = -1;

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
    graphics_draw_string(x + 8, y + (TITLEBAR_H - 16) / 2, "Save", 0xFFFFFFFF);

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

    graphics_draw_string(x + 12, y + 154, "Name:", C_TEXT_DIM);
    graphics_fill_rect(x + 60, y + 150, w - 72, 22, 0xFF101216);
    graphics_draw_rect(x + 60, y + 150, w - 72, 22, C_ACTIVE_DK);
    graphics_draw_string(x + 66, y + 154, dialog_save_name, C_TEXT);
    graphics_fill_rect(x + 66 + dialog_name_len * 8, y + 155, 1, 14, C_TEXT);

    int by = y + h - 32;
    uint32_t sbg = dialog_hover_btn == 1 ? C_BTN_HOVER : 0xFF3B8B3B;
    graphics_fill_rect(x + w - 142, by, 60, 22, sbg);
    graphics_draw_rect(x + w - 142, by, 60, 22, C_PANEL_LINE);
    graphics_draw_string(x + w - 142 + 17, by + 4, "Save", 0xFFFFFFFF);

    uint32_t cbg = dialog_hover_btn == 2 ? C_BTN_HOVER : C_BTN;
    graphics_fill_rect(x + w - 76, by, 64, 22, cbg);
    graphics_draw_rect(x + w - 76, by, 64, 22, C_PANEL_LINE);
    graphics_draw_string(x + w - 76 + 18, by + 4, "Cancel", 0xFFFFFFFF);
}

int gui_save_dialog(const char *suggested, char *out_path, int out_max) {
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

    save_dialog_active = true;
    save_result = 0;
    dialog_hover_btn = 0;
    dialog_hover_place = -1;

    struct pointer_state ps;
    bool dlg_prev_left = false;
    if (pointer_poll(&ps))
        dlg_prev_left = (ps.buttons & 0x01) != 0;

    redraw_pending = true;
    gui_redraw();

    while (save_result == 0) {
        if (pointer_poll(&ps)) {
            bool changed = false;

            if (ps.dx || ps.dy) {
                if (prev_cursor_x >= 0) cursor_restore_bg(prev_cursor_x, prev_cursor_y);
                mouse_x += ps.dx;
                mouse_y += ps.dy;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x >= (int)graphics_get_width()) mouse_x = (int)graphics_get_width() - 1;
                if (mouse_y >= (int)graphics_get_height()) mouse_y = (int)graphics_get_height() - 1;
                changed = true;
            }

            bool left = (ps.buttons & 0x01) != 0;
            if (left && !dlg_prev_left) {
                int place = dialog_place_at(mouse_x, mouse_y);
                int btn = dialog_btn_at(mouse_x, mouse_y);
                if (place >= 0) {
                    gui_str_cpy(dialog_save_dir, dialog_places[place].path,
                                (int)sizeof(dialog_save_dir));
                } else if (btn == 1 && dialog_name_len > 0) {
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

            if (changed) redraw_pending = true;
        }

        if (keyboard_has_input()) {
            char k = 0;
            if (keyboard_read_char(&k)) {
                if (k == '\n' || k == '\r') {
                    if (dialog_name_len > 0) save_result = 1;
                } else if (k == 0x1b) {
                    save_result = -1;
                } else if (k == '\b') {
                    if (dialog_name_len > 0) dialog_save_name[--dialog_name_len] = '\0';
                    redraw_pending = true;
                } else if (k >= 32 && dialog_name_len < DIALOG_NAME_MAX && k != '/') {
                    dialog_save_name[dialog_name_len++] = k;
                    dialog_save_name[dialog_name_len] = '\0';
                    redraw_pending = true;
                }
            }
        }

        if (redraw_pending) {
            redraw_pending = false;
            gui_redraw();
        }
        __asm__ volatile("hlt");
    }

    int r = (save_result == 1) ? 1 : 0;

    if (r == 1) {
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
    redraw_pending = true;
    gui_redraw();

    return r;
}

/* ------------------------------------------------------------------ */
/* Redraw                                                              */
/* ------------------------------------------------------------------ */
void gui_redraw(void) {
    if (bg_cache_valid) {
        bg_cache_blit();
    } else {
        gui_draw_desktop();
        bg_cache_capture();
    }

    /* windows bottom-up by z-order */
    for (int zi = zcount - 1; zi >= 0; zi--) {
        int i = zorder[zi];
        gui_draw_window(&windows[i]);
        gui_draw_window_content(&windows[i]);
    }

    gui_draw_panel();
    if (start_menu_open) gui_draw_start_menu();
    if (save_dialog_active) gui_draw_save_dialog();

    cursor_save_bg(mouse_x, mouse_y);
    graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
    prev_cursor_x = mouse_x;
    prev_cursor_y = mouse_y;
    redraw_pending = false;
}

bool gui_needs_redraw(void) { return redraw_pending; }

void gui_toggle_start_menu(void) {
    start_menu_open = !start_menu_open;
    menu_hover = -1;
    redraw_pending = true;
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
    gui_mouse_prev_left = left_down;

    int sw = graphics_get_width();
    int sh = graphics_get_height();
    int py = sh - PANEL_H;

    /* update pointer position */
    if (dx || dy) {
        if (prev_cursor_x >= 0) cursor_restore_bg(prev_cursor_x, prev_cursor_y);
        mouse_x += dx; mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= sw) mouse_x = sw - 1;
        if (mouse_y >= sh) mouse_y = sh - 1;
        cursor_save_bg(mouse_x, mouse_y);
        graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
        prev_cursor_x = mouse_x;
        prev_cursor_y = mouse_y;
    }

    /* hover tracking: start menu */
    if (start_menu_open) {
        int hov = menu_item_at(mouse_x, mouse_y);
        if (hov != menu_hover) { menu_hover = hov; redraw_pending = true; }
    }
    /* hover tracking: panel */
    if (mouse_y >= py) {
        int new_logo = (mouse_x >= 4 && mouse_x < 4 + LOGO_W) ? 1 : 0;
        if (new_logo != hover_logo) { hover_logo = new_logo; redraw_pending = true; }
        int new_task = task_at(mouse_x, mouse_y);
        if (new_task != hover_task) { hover_task = new_task; redraw_pending = true; }
    }

    /* continue dragging / resizing */
    if (left_down && drag_window >= 0) {
        struct gui_window *w = &windows[drag_window];
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
                redraw_pending = true;
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
            redraw_pending = true;
        }
        mouse_buttons = buttons;
        return;
    }

    /* release ends drag / resize */
    if (left_release && drag_window >= 0) {
        windows[drag_window].dragging = false;
        windows[drag_window].resizing = false;
        drag_window = -1;
        redraw_pending = true;
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
            redraw_pending = true;
            mouse_buttons = buttons;
            return;
        }
        start_menu_open = false;
        redraw_pending = true;
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

    /* panel */
    if (mouse_y >= py) {
        if (mouse_x >= 4 && mouse_x < 4 + LOGO_W) {
            gui_toggle_start_menu();
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
            redraw_pending = true;
            return;
        }
    }
    if (key == '\t' && keyboard_is_alt_pressed()) {
        gui_cycle_focus();
        return;
    }
    if (active_window < 0 || active_window >= MAX_WINDOWS) return;
    if (!windows[active_window].visible || windows[active_window].minimized) return;
    int slot = windows[active_window].app_id - 1;
    const struct app_exports *app = spx_get_exports(slot);
    if (app && app->handle_key) app->handle_key(key);
    redraw_pending = true;
}

void gui_update_clock(void) {
    if (redraw_pending) return;
    int sw = graphics_get_width();
    int sh = graphics_get_height();
    int py = sh - PANEL_H;
    int tw = tray_width();
    int tx = sw - 8 - tw;
    if (prev_cursor_x >= 0) cursor_restore_bg(prev_cursor_x, prev_cursor_y);
    graphics_fill_rect(tx, py, tw, PANEL_H, C_PANEL);
    graphics_fill_rect(tx, py, tw, 1, C_PANEL_LINE);
    gui_draw_tray();
    if (prev_cursor_x >= 0) {
        cursor_save_bg(mouse_x, mouse_y);
        graphics_draw_mouse_cursor(mouse_x, mouse_y, 0xFFFFFFFF);
    }
}

bool gui_take_logout(void) {
    bool r = logout_requested;
    logout_requested = false;
    return r;
}

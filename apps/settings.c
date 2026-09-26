#include <solis/apps/settings.h>
#include <solis/graphics.h>
#include <solis/gui.h>
#include <solis/solfs.h>
#include <solis/ata.h>
#include <solis/rtc.h>
#include <solis/spx.h>

#define SIDEBAR_W 140
#define TAB_H 36
#define COLORS 7
#define NUM_TABS 5
#define HEADER_H 30
#define SCROLL_STEP 32

static int settings_tab = 0;
static int sel_color = 0;
static int hover_tab = -1;
static int hover_tz = -1;

/* The content of the active tab can be taller than the client area (the
 * timezone list alone is 14 rows), so it scrolls. Offsets are per tab so
 * switching back and forth keeps your place. */
static int tab_scroll[NUM_TABS];

/* All tab geometry in one place. The draw code and the hit testing used to
 * compute these positions independently, which is how they drifted apart. */
struct layout {
    int cx, cy;         /* content area origin, before scrolling */
    int cw, ch;         /* content area size */
    int view_h;         /* visible height */
    int content_h;      /* total height of the active tab's content */
    int scroll;         /* clamped scroll offset */

    int theme_y, theme_w, theme_h, theme_start, theme_cols, theme_rows;
    int swatch_y, swatch_w, swatch_h, swatch_gap, swatch_start;
    int tz_y;
};

static int theme_count(void) { return 7; }

static void clamp_scroll(int content_h, int view_h) {
    int maxs = content_h - view_h;
    if (maxs < 0) maxs = 0;
    int s = tab_scroll[settings_tab];
    if (s < 0) s = 0;
    if (s > maxs) s = maxs;
    tab_scroll[settings_tab] = s;
}

static void compute_layout(struct layout *L, int x, int y, int w, int h) {
    L->cx = x + SIDEBAR_W + 1;
    L->cy = y + HEADER_H;
    L->cw = w - SIDEBAR_W - 1;
    L->view_h = h - HEADER_H;
    if (L->view_h < 1) L->view_h = 1;

    /* Four-column preview grid keeps each wallpaper large enough to compare. */
    int nthemes = theme_count();
    L->theme_cols = 4;
    L->theme_rows = (nthemes + L->theme_cols - 1) / L->theme_cols;
    int tw = (L->cw - 36) / L->theme_cols;
    if (tw > 116) tw = 116;
    L->theme_w = tw;
    L->theme_h = tw * 3 / 4;
    if (L->theme_h > 90) L->theme_h = 90;
    L->theme_start = L->cx + (L->cw - (L->theme_cols * tw + (L->theme_cols - 1) * 8)) / 2;
    L->theme_y = L->cy + 36;

    L->swatch_w = 50;
    L->swatch_h = 50;
    L->swatch_gap = 10;
    L->swatch_y = L->theme_y + L->theme_rows * L->theme_h +
                  (L->theme_rows - 1) * 8 + 30;
    L->swatch_start = L->cx + (L->cw - (4 * 50 + 3 * 10)) / 2;

    L->tz_y = L->cy + 86;

    switch (settings_tab) {
        case 0: L->content_h = L->swatch_y - L->cy + 2 * (50 + 10 + 16) + 8; break;
        case 1: L->content_h = 16 + 6 * 22 + 16; break;
        case 2: L->content_h = 16 + 5 * 22 + 16; break;
        case 3: L->content_h = 16 + 22 + 22 + 26 + rtc_get_timezone_count() * 20 + 8; break;
        case 4: L->content_h = 30 + 24 + 20 + 20 + 28 + 1 + 12 + 3 * 16 + 8; break;
        default: L->content_h = 0; break;
    }

    int maxs = L->content_h - L->view_h;
    if (maxs < 0) maxs = 0;
    clamp_scroll(L->content_h, L->view_h);
    L->scroll = tab_scroll[settings_tab];
}

static const uint32_t color_presets[COLORS] = {
    0xFFADD8E6, 0xFF2C2F33, 0xFF1B3B4A,
    0xFF4A3B1B, 0xFF2D1B4A, 0xFF1B4A2D, 0xFF5C5C5C,
};
static const char *color_names[] = {
    "Sky Blue", "Solis Slate", "Deep Ocean",
    "Warm Sepia", "Twilight", "Forest", "Steel Gray",
};

static uint32_t lerp_c(uint32_t c1, uint32_t c2, int t, int m) {
    uint8_t r1 = (c1>>16)&0xFF, g1 = (c1>>8)&0xFF, b1 = c1&0xFF;
    uint8_t r2 = (c2>>16)&0xFF, g2 = (c2>>8)&0xFF, b2 = c2&0xFF;
    uint8_t r = r1 + ((r2-r1)*t/m);
    uint8_t g = g1 + ((g2-g1)*t/m);
    uint8_t b = b1 + ((b2-b1)*t/m);
    return 0xFF000000|(r<<16)|(g<<8)|b;
}

static void draw_preview_sunset(int x, int y, int w, int h) {
    int horizon = h * 3 / 5;
    for (int row = 0; row < horizon; row++)
        graphics_fill_rect(x, y+row, w, 1, lerp_c(0xFF0D0221, 0xFFFFD080, row, horizon));
    for (int row = horizon; row < h; row++)
        graphics_fill_rect(x, y+row, w, 1, 0xFF1A0520);
    int scx = x + w*2/5, scy = y + horizon - 10;
    for (int r = 20; r >= 4; r -= 2)
        graphics_fill_circle(scx, scy, r, lerp_c(0xFFFFF8E0, 0xFFFF6020, r, 20));
    graphics_fill_circle(scx, scy, 3, 0xFFFFF8E0);
    graphics_fill_circle(x + w/4, y + horizon*2/5, 6, 0xFFFFA060);
    graphics_fill_circle(x + w/4 + 8, y + horizon*2/5 - 2, 5, 0xFFFFA060);
    graphics_fill_circle(x + w*3/4, y + horizon/3, 7, 0xFFFF8850);
    graphics_fill_circle(x + w*3/4 + 9, y + horizon/3 - 2, 6, 0xFFFF8850);
    graphics_draw_string(x + w/2 - 16, y + h - 14, "Sunset", 0xFFDDDDDD);
}

static void draw_preview_cherry(int x, int y, int w, int h) {
    int horizon = h * 55 / 100;
    for (int row = 0; row < horizon; row++)
        graphics_fill_rect(x, y+row, w, 1, lerp_c(0xFF87CEEB, 0xFFE8B8D0, row, horizon));
    for (int row = horizon; row < h; row++)
        graphics_fill_rect(x, y+row, w, 1, 0xFF6B9B5A);
    graphics_fill_circle(x + w/2, y + horizon + 8, 30, 0xFF8FA8A0);
    int tx = x + w/2;
    int ty = y + h - 6;
    graphics_fill_rect(tx-2, ty-12, 4, 12, 0xFF3D1F0A);
    graphics_fill_rect(tx-8, ty-16, 16, 3, 0xFF3D1F0A);
    graphics_fill_rect(tx+6, ty-14, 8, 3, 0xFF3D1F0A);
    graphics_fill_circle(tx, ty-16, 4, 0xFFFFB7C5);
    graphics_fill_circle(tx-8, ty-18, 4, 0xFFFF8FAB);
    graphics_fill_circle(tx+10, ty-15, 3, 0xFFFFB7C5);
    graphics_fill_circle(tx-4, ty-20, 3, 0xFFFF6B8F);
    graphics_fill_circle(tx+4, ty-21, 3, 0xFFFFB7C5);
    graphics_draw_string(x + w/2 - 24, y + h - 14, "Cherry Blsm", 0xFFDDDDDD);
}

static void draw_preview_starfield(int x, int y, int w, int h) {
    for (int row = 0; row < h; row++)
        graphics_fill_rect(x, y+row, w, 1, lerp_c(0xFF0D0D2B, 0xFF1A0A3E, row, h));
    graphics_fill_rect(x + w/3 - 10, y + h/4 - 10, 20, 20, lerp_c(0, 0x223366FF, 5, 20));
    graphics_fill_rect(x + w*2/3 - 8, y + h*2/3 - 8, 16, 16, lerp_c(0, 0x226633AA, 5, 16));
    uint32_t sd = 42;
    for (int i = 0; i < 12; i++) {
        sd = sd * 1103515245 + 12345;
        int sx = x + ((sd>>16) % w);
        sd = sd * 1103515245 + 12345;
        int sy = y + ((sd>>16) % h);
        graphics_put_pixel(sx, sy, 0xFFFFFFFF);
    }
    graphics_draw_string(x + w/2 - 20, y + h - 14, "Solis Space", 0xFFDDDDDD);
}

static void draw_preview_gnome(int x, int y, int w, int h) {
    for (int row = 0; row < h; row++)
        graphics_fill_rect(x, y+row, w, 1, lerp_c(0xFF1E1E30, 0xFF0F0F17, row, h));
    for (int r = 32; r > 0; r -= 2)
        graphics_fill_circle(x + w/3, y + h/3, r, lerp_c(0xFF3A5A86, 0xFF0F0F17, r, 32));
    for (int r = 24; r > 0; r -= 2)
        graphics_fill_circle(x + w*2/3, y + h*2/3, r, lerp_c(0xFF5A4A8C, 0xFF0F0F17, r, 24));
    graphics_fill_rect(x + w/2 - 14, y + 4, 28, 6, 0xFF2A2A34);
    graphics_draw_string(x + w/2 - 14, y + h - 14, "Solis Dark", 0xFFDDDDDD);
}

static void draw_preview_solid(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, h, 0xFF2C2F33);
    int wx = x + w/2 - 26, wy = y + h/2 - 12;
    graphics_fill_rect(wx, wy, 52, 24, 0xFF202225);
    graphics_draw_rect(wx, wy, 52, 24, 0xFF1B1E22);
    graphics_fill_rect(wx, wy, 52, 10, 0xFF4C7BD9);
    graphics_fill_rect(wx + 40, wy + 13, 6, 6, 0xFFEAEAEA);
    graphics_draw_string(x + w/2 - 20, y + h - 14, "Solid", 0xFFDDDDDD);
}

static void draw_preview_bored(int x, int y, int w, int h) {
    for (int row = 0; row < h; row++)
        graphics_fill_rect(x, y + row, w, 1, lerp_c(0xFF707CAF, 0xFF5C699F, row, h));
    for (int row = 0; row < h / 5; row++)
        graphics_fill_rect(x, y + row, w * row / (h / 5), 1, 0xFF202D80);
    for (int row = 0; row < h / 3; row++)
        graphics_fill_rect(x + w - w * row / (h / 3), y + row, w * row / (h / 3), 1, 0xFF253381);
    graphics_fill_rect(x + w / 2 - 16, y + h / 2 + 5, 32, 4, 0xFFFFFFFF);
    graphics_draw_string(x + w / 2 - 12, y + h - 14, "Bored", 0xFFFFFFFF);
}

static void draw_preview_aurora(int x, int y, int w, int h) {
    for (int row = 0; row < h; row++)
        graphics_fill_rect(x, y + row, w, 1, lerp_c(0xFF122334, 0xFF20213E, row, h));
    for (int r = h / 2; r > 2; r -= 2)
        graphics_fill_circle(x + w * 3 / 5, y + h * 2 / 5, r,
                             lerp_c(0xFF58D6B0, 0xFF16283B, r, h / 2));
    graphics_fill_rect(x, y + h * 3 / 4, w / 3, h / 4, 0xFF176B69);
    graphics_fill_rect(x + w / 3, y + h * 2 / 3, w / 3, h / 3, 0xFF245787);
    graphics_fill_rect(x + w * 2 / 3, y + h * 3 / 4, w / 3, h / 4, 0xFF4A3B72);
    graphics_draw_string(x + w / 2 - 20, y + h - 14, "Aurora", 0xFFFFFFFF);
}

static void draw_appearance_tab(const struct layout *L) {
    int x = L->cx;
    int py = L->cy + 12 - L->scroll;
    graphics_draw_string(x + 8, py, "Background Theme", 0xFFCCCCCC);
    py += 24;

    struct { int id; void (*draw)(int,int,int,int); } themes[] = {
        {GUI_THEME_SOLID, draw_preview_solid},
        {GUI_THEME_GNOME, draw_preview_gnome},
        {GUI_THEME_SUNSET, draw_preview_sunset},
        {GUI_THEME_CHERRY_BLOSSOM, draw_preview_cherry},
        {GUI_THEME_STARFIELD, draw_preview_starfield},
        {GUI_THEME_BORED, draw_preview_bored},
        {GUI_THEME_AURORA, draw_preview_aurora},
    };
    int nthemes = theme_count();
    int tw = L->theme_w, th = L->theme_h;

    int cur = gui_get_theme();
    for (int i = 0; i < nthemes; i++) {
        int col = i % L->theme_cols, grid_row = i / L->theme_cols;
        int cx = L->theme_start + col * (tw + 8);
        int cy = py + grid_row * (th + 8);
        themes[i].draw(cx, cy, tw, th);
        if (cur == themes[i].id) {
            graphics_draw_rect(cx-1, cy-1, tw+2, th+2, 0xFFFFFFFF);
            graphics_draw_rect(cx-2, cy-2, tw+4, th+4, 0xFF4A90E2);
        } else {
            graphics_draw_rect(cx, cy, tw, th, 0xFF666666);
        }
    }

    py += L->theme_rows * th + (L->theme_rows - 1) * 8 + 24;
    graphics_draw_string(x + 8, py, "Solid Colors", 0xFFCCCCCC);
    py += 20;

    int cols = 4;
    int sw = L->swatch_w, sh = L->swatch_h, gap = L->swatch_gap;
    int col_start = L->swatch_start;
    for (int i = 0; i < COLORS; i++) {
        int cx = col_start + (i % cols) * (sw + gap);
        int cy = py + (i / cols) * (sh + gap + 16);
        graphics_fill_rect(cx, cy, sw, sh, color_presets[i]);
        graphics_draw_rect(cx, cy, sw, sh, i == sel_color ? 0xFFFFFFFF : 0xFF666666);
        if (i == sel_color)
            graphics_draw_rect(cx-1, cy-1, sw+2, sh+2, 0xFFFFFFFF);
        graphics_draw_string(cx + 2, cy + sh + 2, color_names[i], 0xFFAAAAAA);
    }
    (void)cur;
}

static void hex64_str(uint64_t val, char *buf) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
}

static void draw_system_tab(const struct layout *L) {
    int x = L->cx;
    int py = L->cy + 16 - L->scroll;
    graphics_draw_string(x + 12, py, "CPU:", 0xFF888888);
    char cpu[48];
    sys_get_cpu_brand(cpu, sizeof(cpu));
    graphics_draw_string(x + 80, py, cpu[0] ? cpu : "Unknown CPU", 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "RAM:", 0xFF888888);
    char ram[20];
    uint32_t mb = sys_get_total_ram();
    char digits[12];
    int dn = 0, rn = 0;
    if (mb == 0) digits[dn++] = '0';
    while (mb > 0) { digits[dn++] = '0' + (mb % 10); mb /= 10; }
    while (dn > 0) ram[rn++] = digits[--dn];
    ram[rn++] = ' '; ram[rn++] = 'M'; ram[rn++] = 'B'; ram[rn] = '\0';
    graphics_draw_string(x + 80, py, ram, 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "Architecture:", 0xFF888888);
    graphics_draw_string(x + 80, py, "x86 (32-bit)", 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "Storage:", 0xFF888888);
    const char *model = ata_present() ? ata_get_model() : 0;
    graphics_draw_string(x + 80, py, model && model[0] ? model : "No ATA device", 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "Disk serial:", 0xFF888888);
    const char *serial = ata_present() ? ata_get_serial() : 0;
    graphics_draw_string(x + 80, py, serial && serial[0] ? serial : "Unavailable", 0xFFCCCCCC);
    py += 22;
    char idbuf[24];
    hex64_str(solfs_get_machine_id(), idbuf);
    char hwid[48] = "HW ID: ";
    int hi = 7;
    for (int i = 0; idbuf[i]; i++) hwid[hi++] = idbuf[i];
    hwid[hi] = '\0';
    graphics_draw_string(x + 12, py, "HW ID:", 0xFF888888);
    graphics_draw_string(x + 80, py, hwid + 7, 0xFFCCCCCC);
}

static void draw_display_tab(const struct layout *L) {
    int x = L->cx;
    int py = L->cy + 16 - L->scroll;
    graphics_draw_string(x + 12, py, "Resolution:", 0xFF888888);
    char res[32];
    int ri = 0;
    uint32_t fw = graphics_get_width();
    char tmp[16];
    int ti = 0;
    uint32_t n = fw;
    while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
    while (ti > 0) res[ri++] = tmp[--ti];
    res[ri++] = 'x';
    ti = 0;
    n = graphics_get_height();
    while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
    while (ti > 0) res[ri++] = tmp[--ti];
    res[ri] = '\0';
    graphics_draw_string(x + 80, py, res, 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "Color Depth:", 0xFF888888);
    graphics_draw_string(x + 80, py, "32-bit (ARGB)", 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "GPU:", 0xFF888888);
    graphics_draw_string(x + 80, py, "VBE Framebuffer", 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "PIT:", 0xFF888888);
    graphics_draw_string(x + 80, py, "100 Hz", 0xFFCCCCCC);
    py += 22;
    graphics_draw_string(x + 12, py, "Input:", 0xFF888888);
    graphics_draw_string(x + 80, py, "PS/2 Keyboard + Mouse", 0xFFCCCCCC);
}

static void draw_about_tab(const struct layout *L) {
    int x = L->cx;
    int w = L->cw;
    int py = L->cy + 30 - L->scroll;
    graphics_draw_string(x + w/2 - 28, py, "Solis OS", 0xFF4A90E2);
    py += 24;
    graphics_draw_string(x + w/2 - 40, py, "Version 1.0", 0xFFCCCCCC);
    py += 20;
    graphics_draw_string(x + w/2 - 48, py, "A 32-bit Hobby OS", 0xFF888888);
    py += 20;
    graphics_draw_string(x + w/2 - 52, py, "Built with love and C", 0xFF888888);
    py += 28;
    graphics_fill_rect(x + w/2 - 60, py, 120, 1, 0xFF444444);
    py += 12;
    graphics_draw_string(x + w/2 - 52, py, "GUI Framework: SolisGUI", 0xFF666666);
    py += 16;
    graphics_draw_string(x + w/2 - 48, py, "Graphics: VBE 1280x960", 0xFF666666);
    py += 16;
    graphics_draw_string(x + w/2 - 44, py, "Font: 8x16 Bitmap", 0xFF666666);
}

static void append_str(char *buf, int *idx, const char *s) {
    while (*s) buf[(*idx)++] = *s++;
}

static void append_int(char *buf, int *idx, int value) {
    char tmp[8];
    int ti = 0;
    if (value == 0) tmp[ti++] = '0';
    while (value > 0) {
        tmp[ti++] = '0' + (value % 10);
        value /= 10;
    }
    while (ti > 0) buf[(*idx)++] = tmp[--ti];
}

static void append_int2(char *buf, int *idx, int value) {
    buf[(*idx)++] = '0' + ((value / 10) % 10);
    buf[(*idx)++] = '0' + (value % 10);
}

static void draw_time_tab(const struct layout *L) {
    int x = L->cx;
    int w = L->cw;
    static const char *weekday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    struct rtc_time t;
    rtc_get_time(&t);

    int py = L->cy + 16 - L->scroll;

    graphics_draw_string(x + 12, py, "Time:", 0xFF888888);
    char tbuf[16];
    int ti = 0;
    append_int2(tbuf, &ti, t.hour);
    tbuf[ti++] = ':';
    append_int2(tbuf, &ti, t.minute);
    tbuf[ti++] = ':';
    append_int2(tbuf, &ti, t.second);
    tbuf[ti] = '\0';
    graphics_draw_string(x + 80, py, tbuf, 0xFFCCCCCC);
    py += 22;

    graphics_draw_string(x + 12, py, "Date:", 0xFF888888);
    char dbuf[40];
    int di = 0;
    int wd = t.weekday % 7;
    int mon = t.month;
    if (mon < 1) mon = 1;
    if (mon > 12) mon = 12;
    append_str(dbuf, &di, weekday_names[wd]);
    dbuf[di++] = ',';
    dbuf[di++] = ' ';
    append_int2(dbuf, &di, t.day);
    dbuf[di++] = ' ';
    append_str(dbuf, &di, month_names[mon - 1]);
    dbuf[di++] = ' ';
    append_int(dbuf, &di, t.year);
    dbuf[di] = '\0';
    graphics_draw_string(x + 80, py, dbuf, 0xFFCCCCCC);
    py += 22;

    graphics_draw_string(x + 12, py, "Time Zone", 0xFFCCCCCC);
    py += 26;

    int n = rtc_get_timezone_count();
    int cur = rtc_get_timezone();
    for (int i = 0; i < n; i++) {
        int ry = py + i * 20;
        uint32_t bg = (i == cur) ? 0xFF3A3A46 : (i == hover_tz ? 0xFF30303A : 0xFF26262E);
        graphics_fill_rect(x + 8, ry, w - 16, 18, bg);
        char zname[24];
        rtc_get_timezone_name(i, zname, 24);
        graphics_draw_string(x + 14, ry + 2, zname, i == cur ? 0xFFFFFFFF : 0xFFCCCCCC);

        int off = rtc_get_timezone_offset(i);
        char obuf[8];
        int oi = 0;
        int oh = off / 60;
        int om = off % 60;
        if (om < 0) om = -om;
        obuf[oi++] = (oh < 0 || off < 0) ? '-' : '+';
        if (oh < 0) oh = -oh;
        append_int2(obuf, &oi, oh);
        obuf[oi++] = ':';
        append_int2(obuf, &oi, om);
        obuf[oi] = '\0';
        int olen = 0;
        while (obuf[olen]) olen++;
        graphics_draw_string(x + w - 16 - olen * 8, ry + 2, obuf, 0xFF999999);
    }
}

void settings_init(void) {
    settings_tab = 0;
    sel_color = 0;
    hover_tab = -1;
    hover_tz = -1;
    for (int i = 0; i < NUM_TABS; i++) tab_scroll[i] = 0;
}

void settings_draw(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, h, 0xFF1E1E1E);

    graphics_fill_rect(x, y, SIDEBAR_W, h, 0xFF252525);
    graphics_fill_rect(x + SIDEBAR_W, y, 1, h, 0xFF3A3A3A);

    const char *tabs[] = {"Appearance", "System", "Display", "Time & Date", "About"};
    int ntabs = NUM_TABS;
    for (int i = 0; i < ntabs; i++) {
        int ty = y + 8 + i * TAB_H;
        uint32_t bg = (i == settings_tab) ? 0xFF3A3A3A : (i == hover_tab ? 0xFF303030 : 0xFF252525);
        uint32_t tc = (i == settings_tab) ? 0xFFFFFFFF : 0xFFAAAAAA;
        graphics_fill_rect(x + 4, ty, SIDEBAR_W - 8, TAB_H - 4, bg);
        if (i == settings_tab)
            graphics_fill_rect(x, ty + 4, 4, TAB_H - 12, 0xFF4A90E2);
        int lx = x + 12;
        graphics_draw_string(lx, ty + 8, tabs[i], tc);
    }

    struct layout L;
    compute_layout(&L, x, y, w, h);

    graphics_fill_rect(L.cx, y, L.cw, 1, 0xFF333333);
    graphics_fill_rect(L.cx, y + 1, L.cw, 28, 0xFF2A2A2A);
    const char *title = tabs[settings_tab];
    int tw2 = 0;
    for (const char *p = title; *p; p++) tw2 += 8;
    graphics_draw_string(L.cx + (L.cw - tw2) / 2, y + 8, title, 0xFFEEEEEE);
    graphics_fill_rect(L.cx, y + 29, L.cw, 1, 0xFF333333);

    switch (settings_tab) {
        case 0: draw_appearance_tab(&L); break;
        case 1: draw_system_tab(&L); break;
        case 2: draw_display_tab(&L); break;
        case 3: draw_time_tab(&L); break;
        case 4: draw_about_tab(&L); break;
    }

    /* Scrollbar, only when the content actually overflows. Drawn over the
     * content, so the content width is reduced by its width. */
    int maxs = L.content_h - L.view_h;
    if (maxs > 0) {
        const int bar_w = 8;
        int track_x = L.cx + L.cw - bar_w;
        graphics_fill_rect(track_x, L.cy, bar_w, L.view_h, 0xFF1A1A1A);
        int thumb_h = L.view_h * L.view_h / L.content_h;
        if (thumb_h < 24) thumb_h = 24;
        int span = L.view_h - thumb_h;
        int thumb_y = L.cy + (span ? L.scroll * span / maxs : 0);
        graphics_fill_rect(track_x, thumb_y, bar_w, thumb_h, 0xFF4A4A4A);
        graphics_fill_rect(track_x, thumb_y, bar_w, 2, 0xFF5E5E5E);
    }
}

void settings_handle_scroll(int x, int y, int w, int h, int notches) {
    (void)x; (void)y;
    /* notches is positive when the wheel is turned up, which moves the
     * content down, the same way a document view does. */
    tab_scroll[settings_tab] -= notches * SCROLL_STEP;

    struct layout L;
    compute_layout(&L, 0, 0, w, h);
}

void settings_handle_key(char key) {
    if (key == '\t') {
        settings_tab = (settings_tab + 1) % NUM_TABS;
    }
}

void settings_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y) {
    hover_tab = -1;
    hover_tz = -1;
    int ntabs = NUM_TABS;
    for (int i = 0; i < ntabs; i++) {
        int ty = y + 8 + i * TAB_H;
        if (mouse_x >= x + 4 && mouse_x < x + SIDEBAR_W - 4 &&
            mouse_y >= ty && mouse_y < ty + TAB_H - 4) {
            hover_tab = i;
            settings_tab = i;
            return;
        }
    }

    struct layout L;
    compute_layout(&L, x, y, w, h);

    if (settings_tab == 0) {
        int nthemes = theme_count();
        int ty = L.theme_y - L.scroll;
        for (int i = 0; i < nthemes; i++) {
            int col = i % L.theme_cols, grid_row = i / L.theme_cols;
            int cx = L.theme_start + col * (L.theme_w + 8);
            int cy = ty + grid_row * (L.theme_h + 8);
            if (mouse_x >= cx && mouse_x < cx + L.theme_w &&
                mouse_y >= cy && mouse_y < cy + L.theme_h) {
                static const int theme_ids[] = {GUI_THEME_SOLID, GUI_THEME_GNOME,
                                                GUI_THEME_SUNSET, GUI_THEME_CHERRY_BLOSSOM,
                                                GUI_THEME_STARFIELD, GUI_THEME_BORED,
                                                GUI_THEME_AURORA};
                gui_set_theme(theme_ids[i]);
                return;
            }
        }

        int sy = L.swatch_y - L.scroll;
        for (int i = 0; i < COLORS; i++) {
            int cx = L.swatch_start + (i % 4) * (L.swatch_w + L.swatch_gap);
            int cy = sy + (i / 4) * (L.swatch_h + L.swatch_gap + 16);
            if (mouse_x >= cx && mouse_x < cx + L.swatch_w &&
                mouse_y >= cy && mouse_y < cy + L.swatch_h) {
                sel_color = i;
                gui_set_bg_color(color_presets[i]);
                return;
            }
        }
    } else if (settings_tab == 3) {
        int n = rtc_get_timezone_count();
        for (int i = 0; i < n; i++) {
            int ry = L.tz_y - L.scroll + i * 20;
            if (mouse_x >= L.cx + 8 && mouse_x < L.cx + L.cw - 8 &&
                mouse_y >= ry && mouse_y < ry + 18) {
                hover_tz = i;
                rtc_set_timezone(i);
                return;
            }
        }
    }
}

#include <nyx/apps/settings.h>
#include <nyx/graphics.h>
#include <nyx/gui.h>
#include <nyx/nofs.h>
#include <nyx/ata.h>

#define COLORS 6

static int settings_tab = 0;
static int sel_color = 0;

static const uint32_t color_presets[COLORS] = {
    0xFFADD8E6,
    0xFF2D2D2D,
    0xFF1B3B4A,
    0xFF4A3B1B,
    0xFF2D1B4A,
    0xFF1B4A2D,
};

static void draw_bg_tab(int x, int y, int w, int h) {
    (void)h;
    const char *names[] = {"Sky Blue", "Charcoal", "Deep Ocean", "Warm Sepia", "Twilight", "Forest"};
    int cols = 3;
    int sw = 80, sh = 60, gap = 12;
    int start_x = x + (w - (cols * sw + (cols - 1) * gap)) / 2;
    int start_y = y + 20;

    for (int i = 0; i < COLORS; i++) {
        int cx = start_x + (i % cols) * (sw + gap);
        int cy = start_y + (i / cols) * (sh + gap);
        graphics_fill_rect(cx, cy, sw, sh, color_presets[i]);
        graphics_draw_rect(cx, cy, sw, sh, i == sel_color ? 0xFFFFFFFF : 0xFF666666);
        if (i == sel_color) {
            graphics_draw_rect(cx - 1, cy - 1, sw + 2, sh + 2, 0xFFFFFFFF);
        }
        graphics_draw_string(cx + 4, cy + sh + 4, names[i], 0xFFCCCCCC);
    }

    int theme_y = start_y + 2 * (sh + gap) + 30;
    graphics_draw_string(x + 8, theme_y, "Themes:", 0xFFCCCCCC);

    int tx = start_x;
    int ty = theme_y + 20;
    int tw = 3 * sw + 2 * gap;
    int th = 60;
    int is_starfield = (gui_get_theme() == GUI_THEME_STARFIELD);

    for (int r = 0; r < th; r++) {
        uint32_t t = r * 0xFF / th;
        uint8_t rb = 0x0D + ((0x1A - 0x0D) * t / 0xFF);
        uint8_t gb = 0x0D + ((0x0A - 0x0D) * t / 0xFF);
        uint8_t bb = 0x2B + ((0x3E - 0x2B) * t / 0xFF);
        graphics_fill_rect(tx, ty + r, tw, 1, 0xFF000000 | (rb << 16) | (gb << 8) | bb);
    }
    uint32_t sseed = 42;
    for (int i = 0; i < 15; i++) {
        sseed = sseed * 1103515245 + 12345;
        int sx = tx + ((sseed >> 16) % tw);
        sseed = sseed * 1103515245 + 12345;
        int sy = ty + ((sseed >> 16) % th);
        sseed = sseed * 1103515245 + 12345;
        uint32_t b = ((sseed >> 16) & 0x7F) + 0x80;
        graphics_put_pixel(sx, sy, 0xFF000000 | (b << 16) | (b << 8) | b);
    }
    graphics_draw_string(tx + 8, ty + 8, "Nyx Space", 0xFFFFFFFF);
    graphics_draw_rect(tx, ty, tw, th, is_starfield ? 0xFFFFFFFF : 0xFF888888);
    if (is_starfield)
        graphics_draw_rect(tx - 1, ty - 1, tw + 2, th + 2, 0xFFFFFFFF);
}

static void hex64_str(uint64_t val, char *buf) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
}

static void draw_hw_tab(int x, int y, int w, int h) {
    (void)w; (void)h;
    int ly = y + 16;
    graphics_draw_string(x + 8, ly, "CPU:  i386 (QEMU)", 0xFFCCCCCC);
    ly += 20;
    graphics_draw_string(x + 8, ly, "RAM:  256 MB", 0xFFCCCCCC);
    ly += 20;
    {
        char buf[64];
        int bi = 0;
        const char *pre = "FB:   ";
        while (*pre) buf[bi++] = *pre++;
        uint32_t fw = graphics_get_width();
        char tmp[16];
        int ti = 0;
        uint32_t n = fw;
        while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
        while (ti > 0) buf[bi++] = tmp[--ti];
        buf[bi++] = 'x';
        ti = 0;
        n = graphics_get_height();
        while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
        while (ti > 0) buf[bi++] = tmp[--ti];
        buf[bi] = '\0';
        graphics_draw_string(x + 8, ly, buf, 0xFFCCCCCC);
    }
    ly += 20;
    graphics_draw_string(x + 8, ly, "Arch: x86 (32-bit)", 0xFFCCCCCC);
    ly += 20;
    graphics_draw_string(x + 8, ly, "GPU:  VBE Framebuffer", 0xFFCCCCCC);
    ly += 20;
    graphics_draw_string(x + 8, ly, "PIT:  100 Hz", 0xFFCCCCCC);
    ly += 20;
    graphics_draw_string(x + 8, ly, "PS/2: Keyboard + Mouse", 0xFFCCCCCC);
    ly += 20;
    {
        char idbuf[24];
        hex64_str(nofs_get_machine_id(), idbuf);
        char hwid[48] = "HW ID: ";
        int hi = 7;
        for (int i = 0; idbuf[i]; i++)
            hwid[hi++] = idbuf[i];
        hwid[hi] = '\0';
        graphics_draw_string(x + 8, ly, hwid, 0xFFCCCCCC);
    }
    ly += 20;
    {
        char fslabel[48] = "FS:   NOFS";
        if (ata_present()) {
            int fi = 10;
            const char *s = " (";
            while (*s) fslabel[fi++] = *s++;
            const char *sn = ata_get_serial();
            while (*sn && fi < 46) fslabel[fi++] = *sn++;
            fslabel[fi++] = ')';
            fslabel[fi] = '\0';
        }
        graphics_draw_string(x + 8, ly, fslabel, 0xFFCCCCCC);
    }
}

void settings_init(void) {
    settings_tab = 0;
    sel_color = 0;
}

void settings_draw(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, h, 0xFF2D2D2D);

    int tab_w = w / 2;
    int tab_h = 28;
    for (int i = 0; i < 2; i++) {
        uint32_t bg = (i == settings_tab) ? 0xFF4A4A4A : 0xFF1A1A1A;
        graphics_fill_rect(x + i * tab_w, y, tab_w, tab_h, bg);
        graphics_draw_rect(x + i * tab_w, y, tab_w, tab_h, 0xFF555555);
        const char *label = (i == 0) ? "Background" : "Hardware";
        int lx = x + i * tab_w + (tab_w - 8 * 10) / 2;
        graphics_draw_string(lx, y + 8, label, 0xFFDDDDDD);
    }

    graphics_fill_rect(x, y + tab_h, w, 1, 0xFF555555);

    if (settings_tab == 0) {
        draw_bg_tab(x, y + tab_h, w, h - tab_h);
    } else {
        draw_hw_tab(x, y + tab_h, w, h - tab_h);
    }
}

void settings_handle_key(char key) {
    if (key == '\t') {
        settings_tab = (settings_tab + 1) % 2;
    }
}

void settings_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y) {
    (void)h;
    if (mouse_y >= y && mouse_y < y + 28) {
        int tab_w = w / 2;
        if (mouse_x >= x && mouse_x < x + tab_w) {
            settings_tab = 0;
        } else if (mouse_x >= x + tab_w && mouse_x < x + w) {
            settings_tab = 1;
        }
        return;
    }
    if (settings_tab == 0) {
        int cols = 3;
        int sw = 80, sh = 60, gap = 12;
        int start_x = x + (w - (cols * sw + (cols - 1) * gap)) / 2;
        int start_y = y + 28 + 20;
        for (int i = 0; i < COLORS; i++) {
            int cx = start_x + (i % cols) * (sw + gap);
            int cy = start_y + (i / cols) * (sh + gap);
            if (mouse_x >= cx && mouse_x < cx + sw && mouse_y >= cy && mouse_y < cy + sh) {
                sel_color = i;
                gui_set_bg_color(color_presets[i]);
                return;
            }
        }

        int theme_y = start_y + 2 * (sh + gap) + 30;
        int tx = start_x;
        int ty = theme_y + 20;
        int tw = 3 * sw + 2 * gap;
        int th = 60;
        if (mouse_x >= tx && mouse_x < tx + tw && mouse_y >= ty && mouse_y < ty + th) {
            gui_set_theme(GUI_THEME_STARFIELD);
        }
    }
}

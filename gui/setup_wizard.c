#include <solis/setup.h>
#include <solis/graphics.h>
#include <solis/keyboard.h>
#include <solis/mouse.h>
#include <solis/usb.h>
#include <solis/rtc.h>
#include <solis/gui.h>
#include <solis/console.h>
#include <solis/brand.h>
#include <stdbool.h>

#define FONT_W 8
#define FONT_H 16
#define THEME_COUNT 9
#define FIELD_COUNT 3

#define C_BG_TOP    0xFF060B16
#define C_BG_BOT    0xFF0B1424
#define C_CARD      0xFF101A2B
#define C_SIDEBAR   0xFF0C1523
#define C_LINE      0xFF28364D
#define C_TEXT      0xFFF2F7FF
#define C_DIM       0xFF93A4BC
#define C_MUTED     0xFF63748C
#define C_ACCENT    0xFF5B8DEF
#define C_ACCENT_DK 0xFF3867C8
#define C_FIELD     0xFF0A1220
#define C_OK        0xFF4FD1B0
#define C_ERROR     0xFFE05B5B

enum {
    STEP_WELCOME,
    STEP_EDITION,
    STEP_ACCOUNT,
    STEP_APPEARANCE,
    STEP_FINISH,
    STEP_DONE
};

enum {
    ERR_NONE,
    ERR_USER,
    ERR_PASS,
    ERR_MATCH,
    ERR_DISK,
    ERR_NO_DISK
};

struct theme_choice {
    int id;
    const char *name;
    uint32_t color_a;
    uint32_t color_b;
};

static const struct theme_choice themes[THEME_COUNT] = {
    {GUI_THEME_SOLIS_UI_ENGINE, "Solis UI",       0xFF07111F, 0xFF2E5D91},
    {GUI_THEME_AURORA,          "Aurora",         0xFF081426, 0xFF215C58},
    {GUI_THEME_SUNSET,          "Sunset",         0xFF1A0A24, 0xFFC35B32},
    {GUI_THEME_CHERRY_BLOSSOM,  "Cherry Blossom", 0xFF567E9C, 0xFFD38A9D},
    {GUI_THEME_STARFIELD,       "Starfield",      0xFF070A1D, 0xFF2D1D55},
    {GUI_THEME_VOXEL,           "Voxel Dusk",     0xFF34375A, 0xFF8D789D},
    {GUI_THEME_BORED,           "BoredOS",        0xFF3F4A7A, 0xFF20295F},
    {GUI_THEME_GNOME,           "Gnome",          0xFF1B1B2A, 0xFF35305A},
    {GUI_THEME_SOLID,           "Solid Slate",    0xFF202A3A, 0xFF34445D},
};

struct layout {
    int card_x, card_y, card_w, card_h, side_w;
    int content_x, content_w;
    int field_x[FIELD_COUNT], field_y[FIELD_COUNT];
    int field_w, field_h;
    int primary_x, primary_y, primary_w, primary_h;
    int back_x, back_y, back_w, back_h;
    int guest_x, guest_y, guest_w, guest_h;
    int edition_y[3], edition_h;
    int theme_x[THEME_COUNT], theme_y[THEME_COUNT];
    int theme_w, theme_h;
    int tz_x, tz_y, tz_value_x, tz_value_w;
    int tz_prev_x, tz_prev_y, tz_next_x, tz_btn_w, tz_btn_h;
};

struct wizard_state {
    int step;
    int focus;
    int theme_index;
    int tz_index;
    int edition;
    int error;
    bool edition_confirmed;
    bool guest;
    bool installing;
    bool finished;
    int mouse_x;
    int mouse_y;
    bool prev_left;
    char username[SETUP_USERNAME_MAX];
    char password[SETUP_PASSWORD_MAX];
    char confirm[SETUP_PASSWORD_MAX];
};

static int text_len(const char *text) {
    int n = 0;
    while (text && text[n]) n++;
    return n;
}

static void copy_text(char *dst, const char *src, int cap) {
    int i = 0;
    for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int text_width(const char *text) {
    return text_len(text) * FONT_W;
}

static bool same_text(const char *a, const char *b) {
    while (a && b && *a && *b && *a == *b) {
        a++;
        b++;
    }
    return a && b && *a == *b;
}

static void center_text(int cx, int y, const char *text, uint32_t color) {
    graphics_draw_string((uint32_t)(cx - text_width(text) / 2),
                         (uint32_t)y, text, color);
}

static bool in_rect(int x, int y, int w, int h, int px, int py) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool username_ok(const char *name) {
    int n = 0;
    if (!name || !name[0]) return false;
    for (const char *p = name; *p; p++) {
        bool valid = (*p >= 'a' && *p <= 'z') ||
                     (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9') ||
                     *p == '.' || *p == '_' || *p == '-';
        if (!valid) return false;
        n++;
    }
    return n > 0;
}

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
    for (int r = radius; r > 0; r -= 6) {
        uint32_t c = mix(C_BG_BOT, color, radius - r, radius);
        graphics_fill_circle((uint32_t)cx, (uint32_t)cy, (uint32_t)r, c);
    }
}

static void draw_solis_mark(int cx, int cy, int size, uint32_t color) {
    int stroke = size / 8;
    if (stroke < 2) stroke = 2;
    int left = cx - size / 3;
    int top = cy - size / 2;
    int middle = cy - stroke / 2;
    int right = cx + size / 3 - stroke;
    graphics_fill_rect((uint32_t)left, (uint32_t)top,
                       (uint32_t)(size * 2 / 3), (uint32_t)stroke, color);
    graphics_fill_rect((uint32_t)left, (uint32_t)top,
                       (uint32_t)stroke, (uint32_t)(size / 2 + stroke), color);
    graphics_fill_rect((uint32_t)left, (uint32_t)middle,
                       (uint32_t)(size * 2 / 3), (uint32_t)stroke, color);
    graphics_fill_rect((uint32_t)right, (uint32_t)middle,
                       (uint32_t)stroke, (uint32_t)(size / 2), color);
    graphics_fill_rect((uint32_t)left, (uint32_t)(cy + size / 2 - stroke),
                       (uint32_t)(size * 2 / 3), (uint32_t)stroke, color);
}

static void draw_background(void) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    graphics_fill_gradient_v(0, 0, (uint32_t)w, (uint32_t)h,
                             C_BG_TOP, C_BG_BOT);
    glow(w * 18 / 100, h * 18 / 100, h * 38 / 100, 0xFF182E52);
    glow(w * 88 / 100, h * 24 / 100, h * 31 / 100, 0xFF292151);
    glow(w * 54 / 100, h * 94 / 100, h * 34 / 100, 0xFF103A44);

    graphics_draw_circle((uint32_t)(w * 84 / 100),
                         (uint32_t)(h * 22 / 100),
                         (uint32_t)(h / 5), 0xFF1A2941);
    graphics_draw_circle((uint32_t)(w * 13 / 100),
                         (uint32_t)(h * 84 / 100),
                         (uint32_t)(h / 4), 0xFF1A2941);

    uint32_t seed = 0xA11CE5u;
    for (int i = 0; i < 46; i++) {
        seed = seed * 1664525u + 1013904223u;
        int sx = (int)((seed >> 8) % (uint32_t)w);
        seed = seed * 1664525u + 1013904223u;
        int sy = (int)((seed >> 8) % (uint32_t)(h * 62 / 100));
        graphics_put_pixel((uint32_t)sx, (uint32_t)sy,
                           (i & 3) ? 0xFF1A2A42 : 0xFF2A4264);
    }

    draw_solis_mark(58, 48, 36, C_ACCENT);
    graphics_draw_string(82, 34, "SOLIS", C_TEXT);
    graphics_draw_string(82, 52, "INSTALLER", C_DIM);

    const char *setup = "SOLIS INSTALLER  /  " SOLIS_VERSION;
    graphics_draw_string((uint32_t)(w - 24 - text_width(setup)),
                         34, setup, C_MUTED);
}

static void compute_layout(struct layout *L) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    int card_w = w - 120;
    int card_h = h - 140;

    if (card_w > 860) card_w = 860;
    if (card_w < 460) card_w = w - 24;
    if (card_h > 570) card_h = 570;
    if (card_h < 380) card_h = h - 48;
    if (card_w < 320) card_w = 320;
    if (card_h < 300) card_h = 300;

    L->card_w = card_w;
    L->card_h = card_h;
    L->card_x = (w - card_w) / 2;
    L->card_y = (h - card_h) / 2 + 16;
    L->side_w = card_w >= 760 ? 190 : 160;

    L->content_x = L->card_x + L->side_w + 30;
    L->content_w = L->card_x + L->card_w - 30 - L->content_x;

    L->field_w = L->content_w;
    if (L->field_w > 520) L->field_w = 520;
    L->field_h = 40;
    for (int i = 0; i < FIELD_COUNT; i++) {
        L->field_x[i] = L->content_x;
        L->field_y[i] = L->card_y + 192 + i * 64;
    }

    L->primary_w = 180;
    L->primary_h = 40;
    L->primary_x = L->content_x + L->content_w - L->primary_w;
    L->primary_y = L->card_y + L->card_h - 62;
    L->back_w = 100;
    L->back_h = 40;
    L->back_x = L->content_x;
    L->back_y = L->primary_y;

    L->guest_w = 190;
    L->guest_h = 36;
    L->guest_x = L->content_x + (L->content_w - L->guest_w) / 2;
    L->guest_y = L->field_y[FIELD_COUNT - 1] + L->field_h + 48;
    L->edition_h = 86;
    for (int i = 0; i < 3; i++)
        L->edition_y[i] = L->card_y + 148 + i * 96;

    int gap = 10;
    int theme_w = (L->content_w - 2 * gap) / 3;
    if (theme_w > 190) theme_w = 190;
    int grid_w = theme_w * 3 + gap * 2;
    int start_x = L->content_x + (L->content_w - grid_w) / 2;
    int start_y = L->card_y + 158;
    L->theme_w = theme_w;
    L->theme_h = 74;
    for (int i = 0; i < THEME_COUNT; i++) {
        L->theme_x[i] = start_x + (i % 3) * (theme_w + gap);
        L->theme_y[i] = start_y + (i / 3) * (L->theme_h + gap);
    }

    L->tz_x = L->content_x;
    L->tz_y = start_y + 3 * (L->theme_h + gap) + 18;
    L->tz_prev_x = L->content_x + 118;
    L->tz_prev_y = L->tz_y;
    L->tz_btn_w = 30;
    L->tz_btn_h = 30;
    L->tz_value_x = L->tz_prev_x + L->tz_btn_w + 8;
    L->tz_value_w = 246;
    L->tz_next_x = L->tz_value_x + L->tz_value_w + 8;
}

static void draw_button(int x, int y, int w, int h, const char *label,
                        bool primary, bool hover, bool focused) {
    uint32_t fill = primary
        ? (hover ? C_ACCENT_DK : C_ACCENT)
        : (hover ? 0xFF263349 : 0xFF1A2333);
    uint32_t border = primary ? 0xFF8EB4FF : C_LINE;
    graphics_fill_rounded_rect((uint32_t)x, (uint32_t)y,
                               (uint32_t)w, (uint32_t)h, 9, fill);
    graphics_draw_rounded_rect((uint32_t)x, (uint32_t)y,
                               (uint32_t)w, (uint32_t)h, 9, border);
    if (focused)
        graphics_draw_rounded_rect((uint32_t)(x - 3), (uint32_t)(y - 3),
                                   (uint32_t)(w + 6), (uint32_t)(h + 6),
                                   12, 0xFFB9D0FF);
    center_text(x + w / 2, y + (h - FONT_H) / 2, label,
                primary ? C_TEXT : C_DIM);
}

static void draw_field(int x, int y, int w, int h, const char *label,
                       const char *value, bool masked, bool focused,
                       bool hovered) {
    graphics_draw_string((uint32_t)x, (uint32_t)(y - 18), label,
                         focused ? C_TEXT : C_DIM);
    uint32_t fill = focused ? 0xFF0D192A : (hovered ? 0xFF0C1727 : C_FIELD);
    graphics_fill_rounded_rect((uint32_t)x, (uint32_t)y,
                               (uint32_t)w, (uint32_t)h, 8, fill);
    graphics_draw_rounded_rect((uint32_t)x, (uint32_t)y,
                               (uint32_t)w, (uint32_t)h, 8,
                               focused ? C_ACCENT : C_LINE);

    int max_chars = (w - 24) / FONT_W;
    int len = text_len(value);
    int start = len > max_chars ? len - max_chars : 0;
    int shown = len - start;
    if (masked) {
        for (int i = 0; i < shown; i++) {
            graphics_draw_string((uint32_t)(x + 12 + i * FONT_W),
                                 (uint32_t)(y + (h - FONT_H) / 2),
                                 "*", C_TEXT);
        }
    } else {
        char visible[SETUP_USERNAME_MAX];
        int n = shown;
        if (n > SETUP_USERNAME_MAX - 1) n = SETUP_USERNAME_MAX - 1;
        for (int i = 0; i < n; i++) visible[i] = value[start + i];
        visible[n] = '\0';
        graphics_draw_string((uint32_t)(x + 12),
                             (uint32_t)(y + (h - FONT_H) / 2),
                             visible, C_TEXT);
    }
    if (focused) {
        int caret_x = x + 12 + shown * FONT_W;
        if (caret_x > x + w - 10) caret_x = x + w - 10;
        graphics_fill_rect((uint32_t)caret_x,
                           (uint32_t)(y + 10), 1, (uint32_t)(h - 20),
                           C_ACCENT);
    }
}

static void draw_sidebar(const struct layout *L, int step) {
    int x = L->card_x;
    int y = L->card_y;
    int w = L->side_w;
    int h = L->card_h;
    graphics_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 18),
                       (uint32_t)(w - 1), (uint32_t)(h - 36), C_SIDEBAR);
    graphics_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                       (uint32_t)(w - 1), 17, C_SIDEBAR);
    graphics_fill_rect((uint32_t)(x + w), (uint32_t)(y + 18),
                       1, (uint32_t)(h - 36), C_LINE);

    graphics_draw_string((uint32_t)(x + 24), (uint32_t)(y + 26),
                         "SETUP", C_ACCENT);
    graphics_fill_rect((uint32_t)(x + 24), (uint32_t)(y + 46),
                       (uint32_t)(w - 48), 1, C_LINE);

    static const char *labels[5] = {"Welcome", "Edition", "Account",
                                    "Look & feel", "Finish"};
    int active = step == STEP_DONE ? 4 : step;
    int gap = (h - 180) / 5;
    if (gap > 62) gap = 62;
    if (gap < 44) gap = 44;
    for (int i = 0; i < 5; i++) {
        int cy = y + 100 + i * gap;
        int cx = x + 38;
        bool done = active > i;
        bool current = active == i;
        uint32_t circle = current ? C_ACCENT : (done ? 0xFF1D5C56 : 0xFF1A2638);
        uint32_t number = current ? C_TEXT : (done ? C_OK : C_DIM);
        graphics_fill_circle((uint32_t)cx, (uint32_t)cy, 13, circle);
        if (!current && !done)
            graphics_draw_circle((uint32_t)cx, (uint32_t)cy, 13, C_LINE);
        char num[2];
        num[0] = (char)('1' + i);
        num[1] = '\0';
        center_text(cx, cy - FONT_H / 2, num, number);
        graphics_draw_string((uint32_t)(x + 62), (uint32_t)(cy - FONT_H / 2),
                             labels[i], current ? C_TEXT : C_DIM);
        if (i < 4) {
            graphics_fill_rect((uint32_t)(cx - 1), (uint32_t)(cy + 14),
                               2, (uint32_t)(gap - 28),
                               done ? 0xFF1D5C56 : C_LINE);
        }
    }

    graphics_draw_string((uint32_t)(x + 24), (uint32_t)(y + h + 10),
                         SOLIS_RELEASE_LINE, C_DIM);
    graphics_draw_string((uint32_t)(x + 24), (uint32_t)(y + h + 28),
                         "A calmer place to compute", C_MUTED);
}

static const char *theme_name(int index) {
    if (index < 0 || index >= THEME_COUNT) return "Solis UI";
    return themes[index].name;
}

static void draw_theme_card(const struct layout *L, int index,
                            bool selected, bool hovered) {
    const struct theme_choice *t = &themes[index];
    int x = L->theme_x[index];
    int y = L->theme_y[index];
    int w = L->theme_w;
    int h = L->theme_h;
    int preview_h = h - 18;
    graphics_fill_gradient_v((uint32_t)x, (uint32_t)y,
                             (uint32_t)w, (uint32_t)preview_h,
                             t->color_a, t->color_b);
    graphics_fill_rect((uint32_t)(x + 10), (uint32_t)(y + 10),
                       (uint32_t)(w - 20), 7, 0xFF0D1625);
    graphics_fill_rect((uint32_t)(x + 10), (uint32_t)(y + 21),
                       (uint32_t)(w - 46), 4, 0xFF7F9FD2);
    graphics_fill_rect((uint32_t)(x + 10), (uint32_t)(y + 29),
                       (uint32_t)(w - 62), 3, 0xFF4B6788);
    graphics_fill_rect((uint32_t)(x + 1), (uint32_t)(y + preview_h),
                       (uint32_t)(w - 2), 17, 0xFF0C1523);
    graphics_draw_string((uint32_t)(x + 7), (uint32_t)(y + preview_h + 1),
                         t->name, selected ? C_TEXT : C_DIM);
    if (selected) {
        graphics_draw_rect((uint32_t)(x - 1), (uint32_t)(y - 1),
                           (uint32_t)(w + 2), (uint32_t)(h + 2), C_ACCENT);
        graphics_draw_rect((uint32_t)(x - 2), (uint32_t)(y - 2),
                           (uint32_t)(w + 4), (uint32_t)(h + 4), 0xFF8EB4FF);
    } else {
        graphics_draw_rect((uint32_t)x, (uint32_t)y,
                           (uint32_t)w, (uint32_t)h,
                           hovered ? 0xFF5B8DEF : C_LINE);
    }
}

static const char *error_text(int error) {
    switch (error) {
        case ERR_USER:  return "Use a name with letters, numbers, dot, dash or underscore.";
        case ERR_PASS:  return "Use at least 4 characters for your password.";
        case ERR_MATCH: return "The passwords do not match.";
        case ERR_DISK:  return "Could not save to the Solis disk. Check that it is attached.";
        case ERR_NO_DISK: return "No blank or Solis disk found. Attach a writable disk and retry.";
        default:        return "";
    }
}

static void draw_error_or_hint(int x, int y, int error) {
    if (error != ERR_NONE) {
        graphics_draw_string((uint32_t)x, (uint32_t)y, error_text(error), C_ERROR);
    } else {
        graphics_draw_string((uint32_t)x, (uint32_t)y,
                             "Your password is stored as a salted hash, not plain text.",
                             C_MUTED);
    }
}

static void draw_disk_status(const struct layout *L, int y) {
    int status = setup_disk_status();
    const char *text;
    uint32_t color;
    switch (status) {
        case SETUP_DISK_READY:
            text = "Disk: existing Solis volume ready";
            color = C_OK;
            break;
        case SETUP_DISK_BLANK:
            text = "Disk: blank volume will be formatted on install";
            color = 0xFFE4B363;
            break;
        default:
            text = "Disk: no compatible Solis volume detected";
            color = C_ERROR;
            break;
    }
    graphics_fill_rounded_rect((uint32_t)L->content_x, (uint32_t)y,
                               (uint32_t)L->content_w, 26, 7, C_FIELD);
    graphics_draw_rounded_rect((uint32_t)L->content_x, (uint32_t)y,
                               (uint32_t)L->content_w, 26, 7, C_LINE);
    graphics_fill_circle((uint32_t)(L->content_x + 14),
                         (uint32_t)(y + 13), 4, color);
    graphics_draw_string((uint32_t)(L->content_x + 26),
                         (uint32_t)(y + 5), text, C_DIM);
}

static void draw_welcome(const struct wizard_state *s, const struct layout *L) {
    int x = L->content_x;
    int y = L->card_y + 62;
    graphics_draw_string((uint32_t)x, (uint32_t)y, SOLIS_RELEASE_LINE, C_ACCENT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 26), "Welcome to Solis", C_TEXT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 50),
                         "Three editions. One calm system. Choose how much", C_DIM);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 68),
                         "of the workshop you want on this computer.", C_DIM);

    struct feature { const char *title; const char *body; uint32_t color; };
    static const struct feature features[3] = {
        {"Choose an edition", "Minimal, Standard or Extra. Required.", 0xFF5B8DEF},
        {"Make it yours", "A local account or a guest session.", 0xFF4FD1B0},
        {"Saved locally", "Your choices are written to the Solis disk.", 0xFFE4B363},
    };
    for (int i = 0; i < 3; i++) {
        int fy = y + 104 + i * 58;
        graphics_fill_rounded_rect((uint32_t)x, (uint32_t)fy, 5, 42, 2,
                                   features[i].color);
        graphics_draw_string((uint32_t)(x + 18), (uint32_t)(fy + 1),
                             features[i].title, C_TEXT);
        graphics_draw_string((uint32_t)(x + 18), (uint32_t)(fy + 21),
                             features[i].body, C_DIM);
    }

    draw_disk_status(L, L->primary_y - 62);

    bool hover = in_rect(L->primary_x, L->primary_y, L->primary_w,
                         L->primary_h, s->mouse_x, s->mouse_y);
    draw_button(L->primary_x, L->primary_y, L->primary_w, L->primary_h,
                "Choose edition", true, hover, true);
    graphics_draw_string((uint32_t)x,
                         (uint32_t)(L->primary_y + 14),
                         "Press Enter or click the button", C_MUTED);
}

static const char *edition_name(int edition) {
    if (edition == SETUP_EDITION_MINIMAL) return "Minimal";
    if (edition == SETUP_EDITION_STANDARD) return "Standard";
    return "Extra";
}

static const char *edition_desc(int edition) {
    if (edition == SETUP_EDITION_MINIMAL)
        return "Terminal, notes, files and settings.";
    if (edition == SETUP_EDITION_STANDARD)
        return "Every built-in app for work and play,";
    return "Everything in Standard, plus real Doom";
}

static const char *edition_desc2(int edition) {
    if (edition == SETUP_EDITION_MINIMAL) return "The essentials, nothing more.";
    if (edition == SETUP_EDITION_STANDARD) return "without the extra game.";
    return "and the shareware episode.";
}

static const char *edition_tag(int edition) {
    if (edition == SETUP_EDITION_MINIMAL) return "LEAN";
    if (edition == SETUP_EDITION_STANDARD) return "BALANCED";
    return "RECOMMENDED  /  DOOM INSIDE";
}

static void draw_edition(const struct wizard_state *s, const struct layout *L) {
    int x = L->content_x;
    int y = L->card_y + 62;
    graphics_draw_string((uint32_t)x, (uint32_t)y, "CHOOSE YOUR EDITION", C_ACCENT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 26),
                         "How much Solis do you want?", C_TEXT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 50),
                         "This step is required. Extra is selected for you.", C_DIM);

    for (int i = 0; i < 3; i++) {
        int edition = i;
        int cy = L->edition_y[i];
        bool selected = s->edition == edition;
        bool hover = in_rect(x, cy, L->content_w, L->edition_h,
                             s->mouse_x, s->mouse_y);
        uint32_t fill = selected ? 0xFF1C2B3D : (hover ? 0xFF182433 : C_FIELD);
        uint32_t line = selected ? C_ACCENT : (hover ? 0xFF3A4D69 : C_LINE);
        graphics_fill_rounded_rect((uint32_t)x, (uint32_t)cy,
                                   (uint32_t)L->content_w,
                                   (uint32_t)L->edition_h, 11, fill);
        graphics_draw_rounded_rect((uint32_t)x, (uint32_t)cy,
                                   (uint32_t)L->content_w,
                                   (uint32_t)L->edition_h, 11, line);
        graphics_fill_circle((uint32_t)(x + 26), (uint32_t)(cy + 27), 13,
                             selected ? C_ACCENT : 0xFF25344A);
        char number[2] = {(char)('1' + i), '\0'};
        graphics_draw_string((uint32_t)(x + 22), (uint32_t)(cy + 19), number, C_TEXT);
        graphics_draw_string((uint32_t)(x + 50), (uint32_t)(cy + 14),
                             edition_name(edition), C_TEXT);
        graphics_draw_string((uint32_t)(x + 50), (uint32_t)(cy + 38),
                             edition_desc(edition), C_DIM);
        graphics_draw_string((uint32_t)(x + 50), (uint32_t)(cy + 56),
                             edition_desc2(edition), C_DIM);
        int tag_len = text_len(edition_tag(edition));
        graphics_draw_string((uint32_t)(x + L->content_w - 18 - tag_len * 8),
                             (uint32_t)(cy + 14), edition_tag(edition),
                             selected ? C_ACCENT : C_MUTED);
        graphics_draw_circle((uint32_t)(x + L->content_w - 26),
                             (uint32_t)(cy + 58), 8,
                             selected ? C_ACCENT : C_LINE);
        if (selected)
            graphics_fill_circle((uint32_t)(x + L->content_w - 26),
                                 (uint32_t)(cy + 58), 4, C_ACCENT);
    }

    bool hover = in_rect(L->primary_x, L->primary_y, L->primary_w,
                         L->primary_h, s->mouse_x, s->mouse_y);
    draw_button(L->primary_x, L->primary_y, L->primary_w, L->primary_h,
                s->edition_confirmed ? "Continue" : "Select an edition",
                s->edition_confirmed, hover, s->edition_confirmed);
    graphics_draw_string((uint32_t)(L->content_x + L->back_w + 18),
                         (uint32_t)(L->primary_y + 14),
                         !s->edition_confirmed ? "Pick 1, 2 or 3 to continue"
                         : s->edition == SETUP_EDITION_EXTRA ? "Extra keeps Doom ready to play"
                         : s->edition == SETUP_EDITION_STANDARD ? "Standard: every app except Doom"
                         : "Minimal: core apps only",
                         C_MUTED);
}

static void draw_account(const struct wizard_state *s, const struct layout *L) {
    int x = L->content_x;
    int y = L->card_y + 62;
    graphics_draw_string((uint32_t)x, (uint32_t)y, "YOUR ACCOUNT", C_ACCENT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 26),
                         "Create your account", C_TEXT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 50),
                         "This account signs you in to this Solis system.", C_DIM);

    draw_field(L->field_x[0], L->field_y[0], L->field_w, L->field_h,
               "User name", s->username, false, s->focus == 0,
               in_rect(L->field_x[0], L->field_y[0], L->field_w,
                       L->field_h, s->mouse_x, s->mouse_y));
    draw_field(L->field_x[1], L->field_y[1], L->field_w, L->field_h,
               "Password", s->password, true, s->focus == 1,
               in_rect(L->field_x[1], L->field_y[1], L->field_w,
                       L->field_h, s->mouse_x, s->mouse_y));
    draw_field(L->field_x[2], L->field_y[2], L->field_w, L->field_h,
               "Confirm password", s->confirm, true, s->focus == 2,
               in_rect(L->field_x[2], L->field_y[2], L->field_w,
                       L->field_h, s->mouse_x, s->mouse_y));

    draw_error_or_hint(L->content_x,
                       L->field_y[2] + L->field_h + 18, s->error);

    bool guest_hover = in_rect(L->guest_x, L->guest_y, L->guest_w,
                               L->guest_h, s->mouse_x, s->mouse_y) || s->focus == 4;
    graphics_fill_rounded_rect((uint32_t)L->guest_x, (uint32_t)L->guest_y,
                               (uint32_t)L->guest_w, (uint32_t)L->guest_h, 8,
                               guest_hover ? 0xFF263349 : 0xFF1A2333);
    graphics_draw_rounded_rect((uint32_t)L->guest_x, (uint32_t)L->guest_y,
                               (uint32_t)L->guest_w, (uint32_t)L->guest_h, 8,
                               guest_hover ? C_ACCENT : C_LINE);
    graphics_draw_string((uint32_t)(L->guest_x +
                         (L->guest_w - text_len("Continue as guest") * 8) / 2),
                         (uint32_t)(L->guest_y + 10),
                         "Continue as guest", C_DIM);

    bool primary_hover = in_rect(L->primary_x, L->primary_y,
                                 L->primary_w, L->primary_h,
                                 s->mouse_x, s->mouse_y);
    bool back_hover = in_rect(L->back_x, L->back_y, L->back_w,
                              L->back_h, s->mouse_x, s->mouse_y);
    draw_button(L->back_x, L->back_y, L->back_w, L->back_h,
                "Back", false, back_hover, false);
    draw_button(L->primary_x, L->primary_y, L->primary_w, L->primary_h,
                "Continue", true, primary_hover, s->focus == 3);
    graphics_draw_string((uint32_t)(L->content_x + L->back_w + 18),
                         (uint32_t)(L->primary_y + 14),
                         "Tab: next    Enter: continue", C_MUTED);
}

static void draw_appearance(const struct wizard_state *s,
                            const struct layout *L) {
    int x = L->content_x;
    int y = L->card_y + 62;
    graphics_draw_string((uint32_t)x, (uint32_t)y, "APPEARANCE", C_ACCENT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 26),
                         "Make it yours", C_TEXT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 50),
                         "Choose a desktop theme. You can change it later.", C_DIM);

    for (int i = 0; i < THEME_COUNT; i++) {
        bool hover = in_rect(L->theme_x[i], L->theme_y[i],
                             L->theme_w, L->theme_h,
                             s->mouse_x, s->mouse_y);
        draw_theme_card(L, i, i == s->theme_index, hover);
    }

    graphics_draw_string((uint32_t)x, (uint32_t)(L->tz_y - 18),
                         "Time zone", s->focus == 1 ? C_TEXT : C_DIM);
    bool prev_hover = in_rect(L->tz_prev_x, L->tz_prev_y,
                              L->tz_btn_w, L->tz_btn_h,
                              s->mouse_x, s->mouse_y);
    bool next_hover = in_rect(L->tz_next_x, L->tz_prev_y,
                              L->tz_btn_w, L->tz_btn_h,
                              s->mouse_x, s->mouse_y);
    draw_button(L->tz_prev_x, L->tz_prev_y, L->tz_btn_w, L->tz_btn_h,
                "<", false, prev_hover, false);
    draw_button(L->tz_next_x, L->tz_prev_y, L->tz_btn_w, L->tz_btn_h,
                ">", false, next_hover, false);

    char tzname[32];
    rtc_get_timezone_name(s->tz_index, tzname, sizeof(tzname));
    graphics_fill_rounded_rect((uint32_t)L->tz_value_x,
                               (uint32_t)L->tz_prev_y,
                               (uint32_t)L->tz_value_w,
                               (uint32_t)L->tz_btn_h, 8, C_FIELD);
    graphics_draw_rounded_rect((uint32_t)L->tz_value_x,
                               (uint32_t)L->tz_prev_y,
                               (uint32_t)L->tz_value_w,
                               (uint32_t)L->tz_btn_h, 8,
                               s->focus == 1 ? C_ACCENT : C_LINE);
    center_text(L->tz_value_x + L->tz_value_w / 2,
                L->tz_prev_y + (L->tz_btn_h - FONT_H) / 2,
                tzname, C_TEXT);
    graphics_draw_string((uint32_t)L->tz_next_x + L->tz_btn_w + 12,
                         (uint32_t)(L->tz_prev_y + 7),
                         "[ ] change", C_MUTED);

    bool primary_hover = in_rect(L->primary_x, L->primary_y,
                                 L->primary_w, L->primary_h,
                                 s->mouse_x, s->mouse_y);
    bool back_hover = in_rect(L->back_x, L->back_y, L->back_w,
                              L->back_h, s->mouse_x, s->mouse_y);
    draw_button(L->back_x, L->back_y, L->back_w, L->back_h,
                "Back", false, back_hover, s->focus == 2);
    draw_button(L->primary_x, L->primary_y, L->primary_w, L->primary_h,
                "Continue", true, primary_hover, s->focus == 3);
    graphics_draw_string((uint32_t)(L->content_x + L->back_w + 18),
                         (uint32_t)(L->primary_y + 14),
                         "Tab: next control    [ ]: change", C_MUTED);
}

static void draw_summary_row(int x, int y, const char *label,
                             const char *value) {
    graphics_draw_string((uint32_t)x, (uint32_t)y, label, C_DIM);
    graphics_draw_string((uint32_t)(x + 112), (uint32_t)y, value, C_TEXT);
}

static void draw_finish(const struct wizard_state *s,
                        const struct layout *L) {
    int x = L->content_x;
    int y = L->card_y + 62;

    if (s->installing) {
        graphics_draw_string((uint32_t)x, (uint32_t)y, "INSTALLING", C_ACCENT);
        graphics_draw_string((uint32_t)x, (uint32_t)(y + 26),
                             s->guest ? "Preparing guest access" : "Writing your account", C_TEXT);
        graphics_draw_string((uint32_t)x, (uint32_t)(y + 50),
                             setup_disk_status() == SETUP_DISK_BLANK
                                 ? "Formatting the blank disk, then saving your choices."
                                 : "Please wait while Solis saves your choices.", C_DIM);
        graphics_fill_rounded_rect((uint32_t)x, (uint32_t)(y + 110),
                                   (uint32_t)L->content_w, 10, 5,
                                   0xFF1A2638);
        graphics_fill_rounded_rect((uint32_t)x, (uint32_t)(y + 110),
                                   (uint32_t)(L->content_w * 3 / 4), 10, 5,
                                   C_ACCENT);
        return;
    }

    if (s->step == STEP_DONE) {
        graphics_fill_circle((uint32_t)(x + 22), (uint32_t)(y + 36),
                             22, 0xFF17473F);
        graphics_draw_circle((uint32_t)(x + 22), (uint32_t)(y + 36),
                             22, C_OK);
        graphics_draw_string((uint32_t)(x + 18), (uint32_t)(y + 28),
                             "OK", C_OK);
        graphics_draw_string((uint32_t)(x + 58), (uint32_t)y,
                             "Solis is ready", C_TEXT);
        graphics_draw_string((uint32_t)x, (uint32_t)(y + 72),
                             s->guest ? "Guest access and preferences are saved."
                                      : "Your account and preferences are saved.", C_DIM);
        graphics_draw_string((uint32_t)x, (uint32_t)(y + 90),
                             s->guest ? "Guest access needs no password and is saved locally."
                                      : "Sign in with the password you just created.", C_DIM);
        draw_summary_row(x, y + 130, "Account", s->username);
        draw_summary_row(x, y + 154, "Edition", edition_name(s->edition));
        draw_summary_row(x, y + 178, "Theme", theme_name(s->theme_index));
        char tzname[32];
        rtc_get_timezone_name(s->tz_index, tzname, sizeof(tzname));
        draw_summary_row(x, y + 202, "Time zone", tzname);

        bool hover = in_rect(L->primary_x, L->primary_y, L->primary_w,
                             L->primary_h, s->mouse_x, s->mouse_y);
        draw_button(L->primary_x, L->primary_y, L->primary_w,
                    L->primary_h, "Continue", true, hover, true);
        graphics_draw_string((uint32_t)x, (uint32_t)(L->primary_y + 14),
                             "Press Enter to finish", C_MUTED);
        return;
    }

    graphics_draw_string((uint32_t)x, (uint32_t)y, "READY TO INSTALL", C_ACCENT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 26),
                         "Review and install", C_TEXT);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 50),
                         s->guest ? "Solis will prepare a guest session and save"
                                  : "Solis will create your local account and save", C_DIM);
    graphics_draw_string((uint32_t)x, (uint32_t)(y + 68),
                         "these choices to the system disk.", C_DIM);

    graphics_fill_rounded_rect((uint32_t)x, (uint32_t)(y + 98),
                               (uint32_t)L->content_w, 142, 12,
                               0xFF0C1523);
    graphics_draw_rounded_rect((uint32_t)x, (uint32_t)(y + 98),
                               (uint32_t)L->content_w, 142, 12, C_LINE);
    draw_summary_row(x + 18, y + 114, "Account", s->username);
    draw_summary_row(x + 18, y + 140, "Edition", edition_name(s->edition));
    draw_summary_row(x + 18, y + 166, "Theme", theme_name(s->theme_index));
    char tzname[32];
    rtc_get_timezone_name(s->tz_index, tzname, sizeof(tzname));
    draw_summary_row(x + 18, y + 192, "Time zone", tzname);
    graphics_draw_string((uint32_t)(x + 18), (uint32_t)(y + 216),
                         s->guest ? "Guest: no password, local session only"
                                  : "Password: stored as a salted hash", C_MUTED);

    draw_disk_status(L, L->primary_y - 62);

    bool primary_hover = in_rect(L->primary_x, L->primary_y,
                                 L->primary_w, L->primary_h,
                                 s->mouse_x, s->mouse_y);
    bool back_hover = in_rect(L->back_x, L->back_y, L->back_w,
                              L->back_h, s->mouse_x, s->mouse_y);
    draw_button(L->back_x, L->back_y, L->back_w, L->back_h,
                "Back", false, back_hover, s->focus == 1);
    draw_button(L->primary_x, L->primary_y, L->primary_w, L->primary_h,
                "Install Solis", true, primary_hover, s->focus == 0);
    if (s->error != ERR_NONE)
        graphics_draw_string((uint32_t)L->content_x,
                             (uint32_t)(L->primary_y - 24),
                             error_text(s->error), C_ERROR);
}

static void draw_card(const struct wizard_state *s, const struct layout *L) {
    graphics_fill_rounded_rect((uint32_t)(L->card_x + 7),
                               (uint32_t)(L->card_y + 9),
                               (uint32_t)L->card_w, (uint32_t)L->card_h,
                               18, 0xFF040811);
    graphics_fill_rounded_rect((uint32_t)L->card_x, (uint32_t)L->card_y,
                               (uint32_t)L->card_w, (uint32_t)L->card_h,
                               18, C_CARD);
    graphics_draw_rounded_rect((uint32_t)L->card_x, (uint32_t)L->card_y,
                               (uint32_t)L->card_w, (uint32_t)L->card_h,
                               18, C_LINE);
    graphics_fill_rounded_rect((uint32_t)(L->card_x + 18),
                               (uint32_t)(L->card_y + 1),
                               (uint32_t)(L->card_w - 36), 3, 1, C_ACCENT);
    draw_sidebar(L, s->step);

    graphics_push_clip((uint32_t)(L->content_x - 4),
                       (uint32_t)(L->card_y + 8),
                       (uint32_t)(L->content_w + 8),
                       (uint32_t)(L->card_h - 18));
    switch (s->step) {
        case STEP_WELCOME:
            draw_welcome(s, L);
            break;
        case STEP_EDITION:
            draw_edition(s, L);
            break;
        case STEP_ACCOUNT:
            draw_account(s, L);
            break;
        case STEP_APPEARANCE:
            draw_appearance(s, L);
            break;
        case STEP_FINISH:
        case STEP_DONE:
            draw_finish(s, L);
            break;
        default:
            break;
    }
    graphics_pop_clip();
}

static void wizard_redraw(struct wizard_state *s) {
    struct layout L;
    compute_layout(&L);
    graphics_reset_clip();
    graphics_begin_frame();
    draw_background();
    draw_card(s, &L);
    graphics_draw_mouse_cursor((uint32_t)s->mouse_x,
                               (uint32_t)s->mouse_y, C_TEXT);
    graphics_end_frame();
}

static void start_install(struct wizard_state *s) {
    s->installing = true;
    wizard_redraw(s);
    int rc = s->guest
                 ? setup_install_guest(themes[s->theme_index].id,
                                       s->tz_index, s->edition)
                 : setup_install(s->username, s->password,
                                 themes[s->theme_index].id, s->tz_index,
                                 s->edition);
    s->installing = false;
    if (rc == 0) {
        s->step = STEP_DONE;
        s->focus = 0;
        s->error = ERR_NONE;
    } else {
        s->step = STEP_FINISH;
        s->error = rc == SETUP_INSTALL_NO_DISK ? ERR_NO_DISK : ERR_DISK;
    }
    wizard_redraw(s);
}

static void wizard_back(struct wizard_state *s) {
    if (s->step == STEP_EDITION) {
        s->step = STEP_WELCOME;
        s->focus = 0;
        s->error = ERR_NONE;
    } else if (s->step == STEP_ACCOUNT) {
        s->step = STEP_EDITION;
        s->focus = 0;
        s->error = ERR_NONE;
    } else if (s->step == STEP_APPEARANCE) {
        s->step = STEP_ACCOUNT;
        s->focus = 3;
        s->error = ERR_NONE;
    } else if (s->step == STEP_FINISH) {
        s->step = STEP_APPEARANCE;
        s->focus = 0;
        s->error = ERR_NONE;
    }
}

static void continue_account(struct wizard_state *s) {
    int pass_len = text_len(s->password);
    if (!username_ok(s->username)) {
        s->error = ERR_USER;
        s->focus = 0;
        return;
    }
    if (pass_len < 4) {
        s->error = ERR_PASS;
        s->focus = 1;
        return;
    }
    if (!same_text(s->password, s->confirm)) {
        s->error = ERR_MATCH;
        s->focus = 2;
        return;
    }
    s->error = ERR_NONE;
    s->guest = false;
    s->step = STEP_APPEARANCE;
    s->focus = 0;
}

static void continue_guest(struct wizard_state *s) {
    s->guest = true;
    copy_text(s->username, "Guest", SETUP_USERNAME_MAX);
    copy_text(s->password, "", SETUP_PASSWORD_MAX);
    copy_text(s->confirm, "", SETUP_PASSWORD_MAX);
    s->error = ERR_NONE;
    s->step = STEP_APPEARANCE;
    s->focus = 0;
}

static void change_theme(struct wizard_state *s, int delta) {
    s->theme_index = (s->theme_index + delta + THEME_COUNT) % THEME_COUNT;
}

static void change_timezone(struct wizard_state *s, int delta) {
    int count = rtc_get_timezone_count();
    if (count <= 0) return;
    s->tz_index = (s->tz_index + delta + count) % count;
}

static void wizard_primary(struct wizard_state *s) {
    switch (s->step) {
        case STEP_WELCOME:
            s->step = STEP_EDITION;
            s->focus = 0;
            break;
        case STEP_EDITION:
            if (s->edition_confirmed) {
                s->step = STEP_ACCOUNT;
                s->focus = 0;
            }
            break;
        case STEP_ACCOUNT:
            continue_account(s);
            break;
        case STEP_APPEARANCE:
            s->step = STEP_FINISH;
            s->focus = 0;
            s->error = ERR_NONE;
            break;
        case STEP_FINISH:
            if (!s->installing) start_install(s);
            break;
        case STEP_DONE:
            s->finished = true;
            break;
        default:
            break;
    }
}

static void wizard_key(struct wizard_state *s, char key) {
    if (s->step == STEP_EDITION) {
        if (key == 27) {
            wizard_back(s);
            return;
        }
        if (key >= '1' && key <= '3') {
            s->edition = key - '1';
            s->edition_confirmed = true;
            return;
        }
        if (key == '\n' || key == '\r') {
            if (s->edition_confirmed) {
                s->step = STEP_ACCOUNT;
                s->focus = 0;
            }
            return;
        }
        return;
    }

    if (s->step == STEP_ACCOUNT && s->focus < FIELD_COUNT) {
        char *field = s->focus == 0 ? s->username :
                      (s->focus == 1 ? s->password : s->confirm);
        int cap = s->focus == 0 ? SETUP_USERNAME_MAX : SETUP_PASSWORD_MAX;
        int len = text_len(field);
        if (key == '\t') {
            s->focus = (s->focus + 1) % 5;
            return;
        }
        if (key == '\n' || key == '\r') {
            if (s->focus < FIELD_COUNT - 1) s->focus++;
            else continue_account(s);
            return;
        }
        if (key == 27) {
            wizard_back(s);
            return;
        }
        if (key == '\b') {
            if (len > 0) field[len - 1] = '\0';
            return;
        }
        if (key >= 32 && key < 127 && len < cap - 1) {
            field[len] = key;
            field[len + 1] = '\0';
        }
        return;
    }

    if (s->step == STEP_ACCOUNT) {
        if (key == '\t') {
            s->focus = (s->focus + 1) % 5;
            return;
        }
        if (key == '\n' || key == '\r') {
            if (s->focus == 4) continue_guest(s);
            else continue_account(s);
            return;
        }
    }

    if (key == 27) {
        wizard_back(s);
        return;
    }
    if (key == '\t') {
        if (s->step == STEP_APPEARANCE) s->focus = (s->focus + 1) % 4;
        else if (s->step == STEP_FINISH) s->focus = (s->focus + 1) % 2;
        return;
    }
    if (s->step == STEP_APPEARANCE) {
        if (key == '\n' || key == '\r') {
            if (s->focus == 2) wizard_back(s);
            else wizard_primary(s);
            return;
        }
        if (s->focus == 0) {
            if (key == '[') change_theme(s, -1);
            if (key == ']') change_theme(s, 1);
        } else if (s->focus == 1) {
            if (key == '[') change_timezone(s, -1);
            if (key == ']') change_timezone(s, 1);
        }
        return;
    }
    if (s->step == STEP_FINISH) {
        if (key == '\n' || key == '\r') {
            if (s->focus == 1) wizard_back(s);
            else wizard_primary(s);
        }
        return;
    }
    if (s->step == STEP_WELCOME || s->step == STEP_DONE) {
        if (key == '\n' || key == '\r') wizard_primary(s);
    }
}

static void wizard_click(struct wizard_state *s, const struct layout *L) {
    int mx = s->mouse_x;
    int my = s->mouse_y;
    bool primary = in_rect(L->primary_x, L->primary_y,
                           L->primary_w, L->primary_h, mx, my);
    bool back = in_rect(L->back_x, L->back_y, L->back_w, L->back_h, mx, my);

    if (s->step == STEP_WELCOME) {
        if (primary) wizard_primary(s);
        return;
    }
    if (s->step == STEP_EDITION) {
        for (int i = 0; i < SETUP_EDITION_COUNT; i++) {
            if (in_rect(L->content_x, L->edition_y[i], L->content_w,
                        L->edition_h, mx, my)) {
                s->edition = i;
                s->edition_confirmed = true;
                s->focus = 0;
                return;
            }
        }
        if (primary && s->edition_confirmed) wizard_primary(s);
        else if (back) wizard_back(s);
        return;
    }
    if (s->step == STEP_ACCOUNT) {
        for (int i = 0; i < FIELD_COUNT; i++) {
            if (in_rect(L->field_x[i], L->field_y[i], L->field_w,
                        L->field_h, mx, my)) {
                s->focus = i;
                return;
            }
        }
        if (primary) {
            continue_account(s);
            return;
        }
        if (in_rect(L->guest_x, L->guest_y, L->guest_w,
                    L->guest_h, mx, my)) {
            continue_guest(s);
            return;
        }
        if (back) wizard_back(s);
        return;
    }
    if (s->step == STEP_APPEARANCE) {
        for (int i = 0; i < THEME_COUNT; i++) {
            if (in_rect(L->theme_x[i], L->theme_y[i], L->theme_w,
                        L->theme_h, mx, my)) {
                s->theme_index = i;
                s->focus = 0;
                return;
            }
        }
        if (in_rect(L->tz_prev_x, L->tz_prev_y, L->tz_btn_w,
                    L->tz_btn_h, mx, my)) {
            s->focus = 1;
            change_timezone(s, -1);
            return;
        }
        if (in_rect(L->tz_next_x, L->tz_prev_y, L->tz_btn_w,
                    L->tz_btn_h, mx, my)) {
            s->focus = 1;
            change_timezone(s, 1);
            return;
        }
        if (primary) {
            wizard_primary(s);
            return;
        }
        if (back) wizard_back(s);
        return;
    }
    if (s->step == STEP_FINISH) {
        if (primary) wizard_primary(s);
        else if (back) wizard_back(s);
        return;
    }
    if (s->step == STEP_DONE) {
        if (primary) s->finished = true;
    }
}

void setup_wizard_run(void) {
    struct wizard_state s;
    uint8_t *raw = (uint8_t *)&s;
    for (uint32_t i = 0; i < sizeof(s); i++) raw[i] = 0;
    s.step = STEP_WELCOME;
    s.focus = 0;
    s.edition = SETUP_EDITION_DEFAULT;
    s.edition_confirmed = false;
    s.guest = false;
    s.theme_index = 0;
    s.tz_index = rtc_get_timezone();
    if (s.tz_index < 0 || s.tz_index >= rtc_get_timezone_count())
        s.tz_index = 0;
    s.mouse_x = (int)graphics_get_width() / 2;
    s.mouse_y = (int)graphics_get_height() / 2;

    char drain;
    while (keyboard_read_char(&drain)) { }

    console_write("[SETUP] first-boot installer started\n");
    wizard_redraw(&s);

    while (!s.finished) {
        usb_poll();
        bool dirty = false;
        struct mouse_state ms;
        mouse_get_state(&ms);
        if (ms.dx || ms.dy) {
            s.mouse_x += ms.dx;
            s.mouse_y += ms.dy;
            int w = (int)graphics_get_width();
            int h = (int)graphics_get_height();
            if (s.mouse_x < 0) s.mouse_x = 0;
            if (s.mouse_y < 0) s.mouse_y = 0;
            if (s.mouse_x >= w) s.mouse_x = w - 1;
            if (s.mouse_y >= h) s.mouse_y = h - 1;
            dirty = true;
        }

        bool left = (ms.buttons & 0x01) != 0;
        if (left && !s.prev_left) {
            struct layout L;
            compute_layout(&L);
            wizard_click(&s, &L);
            dirty = true;
        }
        s.prev_left = left;

        bool key_activity = false;
        while (keyboard_has_input()) {
            char key = 0;
            if (!keyboard_read_char(&key)) break;
            wizard_key(&s, key);
            key_activity = true;
        }
        if (key_activity) dirty = true;

        if (s.finished) break;
        if (dirty) wizard_redraw(&s);
        __asm__ volatile("hlt");
    }
    console_write("[SETUP] first-boot installer finished\n");
}

#include <solis/login.h>
#include <solis/graphics.h>
#include <solis/mouse.h>
#include <solis/keyboard.h>
#include <solis/usb.h>
#include <solis/setup.h>
#include <solis/brand.h>

#define PANEL_W 460
#define PANEL_H 414
#define BTN_W 240
#define BTN_H 44
#define FIELD_H 40

#define C_BG_TOP    0xFF070C18
#define C_BG_BOT    0xFF0D1727
#define C_PANEL     0xFF111A2B
#define C_PANEL_LN  0xFF28364D
#define C_TEXT      0xFFF2F7FF
#define C_TEXT_DIM  0xFF96A6BC
#define C_MUTED     0xFF63748C
#define C_ACCENT    0xFF5B8DEF
#define C_ACCENT_DK 0xFF3867C8
#define C_FIELD     0xFF0A1220
#define C_ERROR     0xFFE05B5B
#define C_OK        0xFF62C28B

static int mouse_x = 512;
static int mouse_y = 384;
static int btn_rect[4];
static int guest_rect[4];
static int field_rect[4];
static char password[SETUP_PASSWORD_MAX];
static int error_code = 0;
static bool password_focus = true;
static bool guest_session = false;

static int text_len(const char *text) {
    int n = 0;
    while (text && text[n]) n++;
    return n;
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

static void glow(int cx, int cy, int radius, uint32_t color) {
    for (int r = radius; r > 0; r -= 5) {
        uint32_t c = mix(C_BG_BOT, color, radius - r, radius);
        graphics_fill_circle((uint32_t)cx, (uint32_t)cy, (uint32_t)r, c);
    }
}

static void draw_background(void) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    graphics_fill_gradient_v(0, 0, (uint32_t)w, (uint32_t)h,
                             C_BG_TOP, C_BG_BOT);
    glow(w * 17 / 100, h * 20 / 100, h * 38 / 100, 0xFF1B3158);
    glow(w * 86 / 100, h * 27 / 100, h * 30 / 100, 0xFF2B2350);
    glow(w * 52 / 100, h * 94 / 100, h * 34 / 100, 0xFF123C45);

    graphics_draw_circle((uint32_t)(w * 82 / 100),
                         (uint32_t)(h * 21 / 100),
                         (uint32_t)(h / 5), 0xFF1A2941);
    graphics_draw_circle((uint32_t)(w * 15 / 100),
                         (uint32_t)(h * 82 / 100),
                         (uint32_t)(h / 4), 0xFF1A2941);

    uint32_t seed = 0x91C0DEu;
    for (int i = 0; i < 42; i++) {
        seed = seed * 1664525u + 1013904223u;
        int sx = (int)((seed >> 8) % (uint32_t)w);
        seed = seed * 1664525u + 1013904223u;
        int sy = (int)((seed >> 8) % (uint32_t)(h * 58 / 100));
        graphics_put_pixel((uint32_t)sx, (uint32_t)sy,
                           (i & 3) ? 0xFF1A2A42 : 0xFF2A4264);
    }
}

static void draw_login_scene(bool button_hovered, bool field_hovered) {
    int w = (int)graphics_get_width();
    int h = (int)graphics_get_height();
    int cx = w / 2;
    int logo_y = h / 6;

    draw_background();

    draw_solis_mark(cx + 3, logo_y + 4, 54, 0xFF050A12);
    draw_solis_mark(cx, logo_y, 54, C_ACCENT);
    graphics_draw_string((uint32_t)(cx - 56), (uint32_t)(logo_y + 36),
                         SOLIS_RELEASE_LINE, C_TEXT);
    graphics_draw_string((uint32_t)(cx - 76), (uint32_t)(logo_y + 58),
                         SOLIS_TAGLINE, C_TEXT_DIM);

    int px = (w - PANEL_W) / 2;
    int py = (h - PANEL_H) / 2 + 30;

    graphics_fill_rounded_rect((uint32_t)(px + 6), (uint32_t)(py + 8),
                               PANEL_W, PANEL_H, 18, 0xFF050A12);
    graphics_fill_rounded_rect((uint32_t)px, (uint32_t)py,
                               PANEL_W, PANEL_H, 18, C_PANEL);
    graphics_draw_rounded_rect((uint32_t)px, (uint32_t)py,
                               PANEL_W, PANEL_H, 18, C_PANEL_LN);
    graphics_fill_rounded_rect((uint32_t)(px + 18), (uint32_t)(py + 1),
                               PANEL_W - 36, 3, 1, C_ACCENT);

    graphics_fill_circle((uint32_t)cx, (uint32_t)(py + 52), 28,
                         C_ACCENT_DK);
    graphics_fill_circle((uint32_t)cx, (uint32_t)(py + 52), 24,
                         0xFF1B2D48);

    const char *username = setup_username();
    char initial = 'U';
    if (username && username[0]) {
        initial = username[0];
        if (initial >= 'a' && initial <= 'z')
            initial = (char)(initial - 'a' + 'A');
    }
    char initial_text[2] = {initial, '\0'};
    graphics_draw_string((uint32_t)(cx - 4), (uint32_t)(py + 44),
                         initial_text, C_TEXT);

    graphics_draw_string((uint32_t)(cx - 56), (uint32_t)(py + 100),
                         "Good to see you", C_TEXT);
    int name_w = text_len(username) * 8;
    graphics_draw_string((uint32_t)(cx - name_w / 2), (uint32_t)(py + 122),
                         username, C_ACCENT);
    graphics_draw_string((uint32_t)(cx - 48), (uint32_t)(py + 144),
                         setup_is_guest() ? "Guest session" : "Local account",
                         C_TEXT_DIM);

    graphics_draw_string((uint32_t)(px + 42), (uint32_t)(py + 172),
                         "Password", C_TEXT_DIM);

    int fx = px + 42;
    int fy = py + 190;
    int fw = PANEL_W - 84;
    field_rect[0] = fx;
    field_rect[1] = fy;
    field_rect[2] = fw;
    field_rect[3] = FIELD_H;
    graphics_fill_rounded_rect((uint32_t)fx, (uint32_t)fy,
                               (uint32_t)fw, FIELD_H, 8,
                               password_focus ? 0xFF0D192A :
                               (field_hovered ? 0xFF0C1727 : C_FIELD));
    graphics_draw_rounded_rect((uint32_t)fx, (uint32_t)fy,
                               (uint32_t)fw, FIELD_H, 8,
                               password_focus ? C_ACCENT : C_PANEL_LN);

    int len = text_len(password);
    int max_chars = (fw - 24) / 8;
    int start = len > max_chars ? len - max_chars : 0;
    int shown = len - start;
    for (int i = 0; i < shown; i++) {
        graphics_draw_string((uint32_t)(fx + 12 + i * 8),
                             (uint32_t)(fy + 12), "*", C_TEXT);
    }
    if (password_focus) {
        int caret_x = fx + 12 + shown * 8;
        if (caret_x > fx + fw - 10) caret_x = fx + fw - 10;
        graphics_fill_rect((uint32_t)caret_x, (uint32_t)(fy + 9),
                           1, FIELD_H - 18, C_ACCENT);
    }

    if (error_code == 1) {
        graphics_draw_string((uint32_t)(px + 42), (uint32_t)(py + 242),
                             "Incorrect password. Try again.", C_ERROR);
    } else if (error_code == 2) {
        graphics_draw_string((uint32_t)(px + 42), (uint32_t)(py + 242),
                             "No account configured. Reboot to run setup.", C_ERROR);
    } else if (setup_is_guest()) {
        graphics_draw_string((uint32_t)(px + 42), (uint32_t)(py + 242),
                             "Guest access is ready. No password needed.", C_OK);
    } else {
        graphics_draw_string((uint32_t)(px + 42), (uint32_t)(py + 242),
                             "Enter your password to unlock this session.", C_MUTED);
    }

    int bx = cx - BTN_W / 2;
    int by = py + 270;
    btn_rect[0] = bx;
    btn_rect[1] = by;
    btn_rect[2] = BTN_W;
    btn_rect[3] = BTN_H;
    uint32_t button = button_hovered ? C_ACCENT_DK : C_ACCENT;
    graphics_fill_rounded_rect((uint32_t)bx, (uint32_t)by,
                               BTN_W, BTN_H, 10, button);
    graphics_draw_rounded_rect((uint32_t)bx, (uint32_t)by,
                               BTN_W, BTN_H, 10, 0xFF8EB4FF);
    const char *sign_label = setup_is_guest() ? "Enter Solis" : "Sign in";
    graphics_draw_string((uint32_t)(bx + (BTN_W - text_len(sign_label) * 8) / 2),
                         (uint32_t)(by + 14), sign_label, C_TEXT);

    int gx = cx - BTN_W / 2;
    int gy = py + 326;
    guest_rect[0] = gx;
    guest_rect[1] = gy;
    guest_rect[2] = BTN_W;
    guest_rect[3] = BTN_H;
    bool guest_hover = mouse_x >= gx && mouse_x < gx + BTN_W &&
                       mouse_y >= gy && mouse_y < gy + BTN_H;
    graphics_fill_rounded_rect((uint32_t)gx, (uint32_t)gy,
                               BTN_W, BTN_H, 10,
                               guest_hover ? 0xFF263349 : 0xFF1A2333);
    graphics_draw_rounded_rect((uint32_t)gx, (uint32_t)gy,
                               BTN_W, BTN_H, 10,
                               guest_hover ? C_ACCENT : C_PANEL_LN);
    graphics_draw_string((uint32_t)(gx + (BTN_W - 17 * 8) / 2),
                         (uint32_t)(gy + 14), "Continue as guest", C_TEXT_DIM);

    graphics_draw_string((uint32_t)(cx - 104), (uint32_t)(py + 380),
                         "Enter signs in  /  guest is local", C_MUTED);
}

static void redraw_login(bool button_hovered, bool field_hovered) {
    graphics_reset_clip();
    graphics_begin_frame();
    draw_login_scene(button_hovered, field_hovered);
    graphics_draw_mouse_cursor((uint32_t)mouse_x, (uint32_t)mouse_y, C_TEXT);
    graphics_end_frame();
}

static bool mouse_in(int x, int y, int w, int h) {
    return mouse_x >= x && mouse_x < x + w &&
           mouse_y >= y && mouse_y < y + h;
}

static bool submit_password(void) {
    if (!setup_is_installed()) {
        error_code = 2;
        return false;
    }
    if (setup_is_guest() && password[0] == '\0') {
        guest_session = true;
        error_code = 0;
        return true;
    }
    if (password[0] && setup_authenticate(password)) {
        password[0] = '\0';
        error_code = 0;
        guest_session = false;
        return true;
    }
    password[0] = '\0';
    error_code = 1;
    return false;
}

bool login_screen(void) {
    uint32_t w = graphics_get_width();
    uint32_t h = graphics_get_height();
    if (mouse_x >= (int)w) mouse_x = (int)w - 1;
    if (mouse_y >= (int)h) mouse_y = (int)h - 1;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;

    password[0] = '\0';
    error_code = 0;
    password_focus = true;
    guest_session = false;

    char drain;
    while (keyboard_read_char(&drain)) { }

    bool button_hovered = false;
    bool field_hovered = false;
    redraw_login(button_hovered, field_hovered);

    bool done = false;
    bool prev_left = false;
    while (!done) {
        usb_poll();

        struct mouse_state ms;
        mouse_get_state(&ms);
        bool dirty = false;
        if (ms.dx || ms.dy) {
            mouse_x += ms.dx;
            mouse_y += ms.dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)w) mouse_x = (int)w - 1;
            if (mouse_y >= (int)h) mouse_y = (int)h - 1;
            button_hovered = mouse_in(btn_rect[0], btn_rect[1],
                                      btn_rect[2], btn_rect[3]);
            field_hovered = mouse_in(field_rect[0], field_rect[1],
                                     field_rect[2], field_rect[3]);
            dirty = true;
        }

        bool left = (ms.buttons & 0x01) != 0;
        if (left && !prev_left) {
            if (mouse_in(field_rect[0], field_rect[1],
                         field_rect[2], field_rect[3])) {
                password_focus = true;
                dirty = true;
            } else if (mouse_in(btn_rect[0], btn_rect[1],
                                btn_rect[2], btn_rect[3])) {
                if (submit_password()) done = true;
                dirty = true;
            } else if (mouse_in(guest_rect[0], guest_rect[1],
                                guest_rect[2], guest_rect[3])) {
                guest_session = true;
                done = true;
            }
        }
        prev_left = left;

        while (keyboard_has_input() && !done) {
            char key = 0;
            if (!keyboard_read_char(&key)) break;
            if (key == '\t') {
                password_focus = true;
            } else if (key == '\b') {
                int len = text_len(password);
                if (len > 0) password[len - 1] = '\0';
            } else if (key == '\n' || key == '\r') {
                if (submit_password()) done = true;
            } else if (key >= 32 && key < 127) {
                int len = text_len(password);
                if (len < SETUP_PASSWORD_MAX - 1) {
                    password[len] = key;
                    password[len + 1] = '\0';
                }
            }
            dirty = true;
        }

        if (done) break;
        if (dirty) redraw_login(button_hovered, field_hovered);
        __asm__ volatile("hlt");
    }
    password[0] = '\0';
    return guest_session;
}

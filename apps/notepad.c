#include <solis/apps/notepad.h>
#include <solis/graphics.h>
#include <solis/solfs.h>

#define NOTE_COLS 60
#define NOTE_ROWS 20
#define NOTE_BUF (NOTE_COLS * NOTE_ROWS)
#define TOOLBAR_H 28

static char note_buffer[NOTE_BUF];
static int note_row = 0;
static int note_col = 0;
static char status_msg[40];
static int status_ticks = 0;

static bool load_panel_open = false;
static int load_file_count = 0;
static char load_file_names[SOLFS_MAX_FILES][SOLFS_MAX_NAME];

void notepad_init(void) {
    for (int i = 0; i < NOTE_BUF; i++)
        note_buffer[i] = ' ';
    note_row = 0;
    note_col = 0;
    status_msg[0] = '\0';
    status_ticks = 0;
    load_panel_open = false;
    load_file_count = 0;
}

static void notepad_scroll(void) {
    for (int i = 0; i < NOTE_BUF - NOTE_COLS; i++)
        note_buffer[i] = note_buffer[i + NOTE_COLS];
    for (int i = NOTE_BUF - NOTE_COLS; i < NOTE_BUF; i++)
        note_buffer[i] = ' ';
    if (note_row > 0) note_row--;
}

static int next_note_number(void) {
    char names[SOLFS_MAX_FILES][SOLFS_MAX_NAME];
    int count = solfs_list(names, SOLFS_MAX_FILES);
    int max_n = 0;
    for (int i = 0; i < count; i++) {
        if (names[i][0] == 'N' && names[i][1] == 'o' &&
            names[i][2] == 't' && names[i][3] == 'e') {
            int n = 0;
            for (int j = 4; names[i][j] >= '0' && names[i][j] <= '9'; j++)
                n = n * 10 + (names[i][j] - '0');
            if (n > max_n) max_n = n;
        }
    }
    return max_n + 1;
}

static void set_status(const char *msg) {
    int i = 0;
    while (msg[i] && i < 39) { status_msg[i] = msg[i]; i++; }
    status_msg[i] = '\0';
    status_ticks = 15;
}

static void save_note(void) {
    int num = next_note_number();
    char fname[24];
    int fi = 0;
    const char *pre = "Note";
    while (*pre) fname[fi++] = *pre++;
    int n = num;
    char rev[12];
    int ri = 0;
    while (n > 0) { rev[ri++] = '0' + (n % 10); n /= 10; }
    if (ri == 0) rev[ri++] = '0';
    while (ri > 0) fname[fi++] = rev[--ri];
    fname[fi] = '\0';

    int fd = solfs_create(fname);
    if (fd >= 0) {
        int len = NOTE_BUF;
        while (len > 0 && note_buffer[len - 1] == ' ') len--;
        solfs_write(fd, (const uint8_t*)note_buffer, len);
    }

    char msg[40];
    int si = 0;
    const char *pre2 = "Saved ";
    while (*pre2) msg[si++] = *pre2++;
    int ni = 0;
    while (fname[ni]) msg[si++] = fname[ni++];
    msg[si] = '\0';
    set_status(msg);
}

static void new_note(void) {
    for (int i = 0; i < NOTE_BUF; i++)
        note_buffer[i] = ' ';
    note_row = 0;
    note_col = 0;
    set_status("New note");
}

static void load_note(const char *name) {
    int fd = solfs_open(name);
    if (fd < 0) { set_status("Can't open file"); return; }
    int sz = solfs_get_size(fd);
    if (sz > NOTE_BUF) sz = NOTE_BUF;

    for (int i = 0; i < NOTE_BUF; i++)
        note_buffer[i] = ' ';

    uint8_t buf[NOTE_BUF];
    int read = solfs_read(fd, buf, sz);

    int pos = 0;
    int row = 0;
    int col = 0;
    while (pos < read && row < NOTE_ROWS) {
        char c = (char)buf[pos++];
        if (c == '\n') {
            col = 0;
            row++;
        } else if (c >= 32) {
            if (col < NOTE_COLS) {
                note_buffer[row * NOTE_COLS + col] = c;
                col++;
            } else {
                col = 0;
                row++;
                if (row < NOTE_ROWS) {
                    note_buffer[row * NOTE_COLS + col] = c;
                    col++;
                }
            }
        }
    }
    note_row = row;
    note_col = col;
    if (note_row >= NOTE_ROWS) note_row = NOTE_ROWS - 1;

    char msg[40];
    int si = 0;
    const char *pre3 = "Loaded ";
    while (*pre3) msg[si++] = *pre3++;
    int ni = 0;
    while (name[ni]) msg[si++] = name[ni++];
    msg[si] = '\0';
    set_status(msg);
    load_panel_open = false;
}

static void refresh_file_list(void) {
    load_file_count = solfs_list(load_file_names, SOLFS_MAX_FILES);
}

void notepad_draw(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, TOOLBAR_H, 0xFFE8E8E8);
    graphics_draw_rect(x, y, w, TOOLBAR_H, 0xFFCCCCCC);

    int btn_y = y + 3;
    int btn_h = 22;

    graphics_fill_rect(x + 4, btn_y, 46, btn_h, 0xFF4A90E2);
    graphics_draw_string(x + 10, btn_y + 4, "New", 0xFFFFFFFF);

    graphics_fill_rect(x + 54, btn_y, 50, btn_h, 0xFF3B8B3B);
    graphics_draw_string(x + 60, btn_y + 4, "Save", 0xFFFFFFFF);

    uint32_t load_btn = load_panel_open ? 0xFF8B5CF6 : 0xFFD4A050;
    graphics_fill_rect(x + 108, btn_y, 50, btn_h, load_btn);
    graphics_draw_string(x + 114, btn_y + 4, "Load", 0xFFFFFFFF);

    if (status_ticks > 0 && status_msg[0])
        graphics_draw_string(x + 170, btn_y + 4, status_msg, 0xFF006600);

    int text_x = x + 2;
    int text_y = y + TOOLBAR_H + 2;
    int text_w = w - 4;
    int text_h = h - TOOLBAR_H - 4;

    int list_w = 0;
    if (load_panel_open) {
        list_w = 160;
        if (list_w > text_w / 2) list_w = text_w / 2;

        graphics_fill_rect(text_x + text_w - list_w, text_y, list_w, text_h, 0xFFFFF8F0);
        graphics_draw_rect(text_x + text_w - list_w, text_y, list_w, text_h, 0xFFCCCCCC);
        graphics_draw_string(text_x + text_w - list_w + 4, text_y + 2, "Files:", 0xFF444444);

        refresh_file_list();

        int item_h = 18;
        int list_start = text_y + 18;
        int visible = (text_h - 22) / item_h;
        if (visible > load_file_count) visible = load_file_count;

        for (int i = 0; i < visible; i++) {
            if (i >= load_file_count) break;
            int iy = list_start + i * item_h;
            graphics_fill_rect(text_x + text_w - list_w + 2, iy, list_w - 4, item_h - 1, 0xFFFFFFFF);
            graphics_draw_string(text_x + text_w - list_w + 6, iy + 2, load_file_names[i], 0xFF333333);
        }
    }

    int text_area_w = text_w - list_w - (list_w > 0 ? 4 : 0);
    graphics_fill_rect(text_x, text_y, text_area_w, text_h, 0xFFFFFFFF);
    graphics_draw_rect(text_x, text_y, text_area_w, text_h, 0xFFCCCCCC);

    int cols = (text_area_w - 8) / 8;
    int rows = (text_h - 8) / 16;
    if (cols > NOTE_COLS) cols = NOTE_COLS;
    if (rows > NOTE_ROWS) rows = NOTE_ROWS;

    int start_row = 0;
    if (note_row >= rows) start_row = note_row - rows + 1;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int bi = (start_row + r) * NOTE_COLS + c;
            if (bi < NOTE_BUF && note_buffer[bi] && note_buffer[bi] != ' ') {
                char str[2] = {note_buffer[bi], '\0'};
                graphics_draw_string(text_x + 4 + c * 8, text_y + 4 + r * 16, str, 0xFF222222);
            }
        }
    }

    int cursor_screen_col = note_col;
    int cursor_screen_row = note_row - start_row;
    if (cursor_screen_row >= 0 && cursor_screen_row < rows && cursor_screen_col < cols) {
        char cursor_str[2] = "|";
        graphics_draw_string(text_x + 4 + cursor_screen_col * 8, text_y + 4 + cursor_screen_row * 16, cursor_str, 0xFF000000);
    }

    if (status_ticks > 0) status_ticks--;
}

void notepad_handle_key(char key) {
    if (key == 0x13) { save_note(); return; }
    if (key == '\n') {
        note_col = 0;
        note_row++;
        if (note_row >= NOTE_ROWS) notepad_scroll();
    } else if (key == '\b') {
        if (note_col > 0) {
            note_col--;
            note_buffer[note_row * NOTE_COLS + note_col] = ' ';
        }
    } else if (key >= 32) {
        if (note_col >= NOTE_COLS) {
            note_col = 0;
            note_row++;
            if (note_row >= NOTE_ROWS) notepad_scroll();
        }
        int idx = note_row * NOTE_COLS + note_col;
        if (idx < NOTE_BUF) {
            note_buffer[idx] = key;
            note_col++;
        }
    }
}

void notepad_handle_mouse(int win_x, int win_y, int win_w, int win_h, int mouse_x, int mouse_y) {
    int rel_x = mouse_x - win_x;
    int rel_y = mouse_y - win_y;

    if (rel_y >= 3 && rel_y < 25) {
        if (rel_x >= 4 && rel_x < 50) { new_note(); return; }
        if (rel_x >= 54 && rel_x < 104) { save_note(); return; }
        if (rel_x >= 108 && rel_x < 158) {
            load_panel_open = !load_panel_open;
            if (load_panel_open) refresh_file_list();
            return;
        }
    }

    if (load_panel_open) {
        int text_x = 2;
        int text_y = TOOLBAR_H + 2;
        int text_w = win_w - 4;
        int text_h = win_h - TOOLBAR_H - 4;
        int list_w = 160;
        if (list_w > text_w / 2) list_w = text_w / 2;

        int list_area_x = text_x + text_w - list_w;
        int list_start_y = text_y + 18;
        int item_h = 18;
        int visible = (text_h - 22) / item_h;

        if (rel_x >= list_area_x && rel_x < list_area_x + list_w &&
            rel_y >= list_start_y && rel_y < list_start_y + visible * item_h) {
            int idx = (rel_y - list_start_y) / item_h;
            if (idx >= 0 && idx < load_file_count) {
                load_note(load_file_names[idx]);
            }
        }
    }
}
#include <nyx/apps/notepad.h>
#include <nyx/graphics.h>

#define NOTE_COLS 60
#define NOTE_ROWS 20
#define NOTE_BUF (NOTE_COLS * NOTE_ROWS)

static char note_buffer[NOTE_BUF];
static int note_row = 0;
static int note_col = 0;

void notepad_init(void) {
    for (int i = 0; i < NOTE_BUF; i++)
        note_buffer[i] = ' ';
    note_row = 0;
    note_col = 0;
}

static void notepad_scroll(void) {
    for (int i = 0; i < NOTE_BUF - NOTE_COLS; i++)
        note_buffer[i] = note_buffer[i + NOTE_COLS];
    for (int i = NOTE_BUF - NOTE_COLS; i < NOTE_BUF; i++)
        note_buffer[i] = ' ';
    if (note_row > 0) note_row--;
}

void notepad_draw(int x, int y, int w, int h) {
    (void)w;
    (void)h;
    int cols = (w - 8) / 8;
    int rows = (h - 8) / 16;
    if (cols > NOTE_COLS) cols = NOTE_COLS;
    if (rows > NOTE_ROWS) rows = NOTE_ROWS;

    int start_row = 0;
    if (note_row >= rows) start_row = note_row - rows + 1;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int bi = (start_row + r) * NOTE_COLS + c;
            if (bi < NOTE_BUF && note_buffer[bi] && note_buffer[bi] != ' ') {
                char str[2] = {note_buffer[bi], '\0'};
                graphics_draw_string(x + 4 + c * 8, y + 4 + r * 16, str, 0xFF222222);
            }
        }
    }

    int cursor_screen_col = note_col;
    int cursor_screen_row = note_row - start_row;
    if (cursor_screen_row >= 0 && cursor_screen_row < rows && cursor_screen_col < cols) {
        char cursor_str[2] = "|";
        graphics_draw_string(x + 4 + cursor_screen_col * 8, y + 4 + cursor_screen_row * 16, cursor_str, 0xFF000000);
    }
}

void notepad_handle_key(char key) {
    if (key == '\n') {
        note_col = 0;
        note_row++;
        if (note_row >= NOTE_ROWS) {
            notepad_scroll();
        }
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

#include <solis/apps/calculator.h>
#include <solis/graphics.h>
#include <stdbool.h>
#include <stdint.h>

#define CALC_W 360
#define CALC_H 330
#define GRID_COLS 4
#define GRID_ROWS 4
#define GRID_GAP 6
#define GRID_TOP 112

static char display[24];
static int display_len;
static int64_t accumulator;
static int64_t memory;
static char pending_op;
static bool entering;
static bool calc_error;

static const char *const keys[GRID_ROWS][GRID_COLS] = {
    {"7", "8", "9", "/"},
    {"4", "5", "6", "*"},
    {"1", "2", "3", "-"},
    {"0", "C", "=", "+"},
};

static void copy_text(char *dst, const char *src, int max) {
    int i = 0;
    for (; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int text_len(const char *text) {
    int n = 0;
    while (text && text[n]) n++;
    return n;
}

static void set_display(int64_t value) {
    char tmp[24];
    int ti = 0;
    bool negative = value < 0;
    uint64_t magnitude = negative ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;

    if (magnitude == 0)
        tmp[ti++] = '0';
    while (magnitude > 0) {
        tmp[ti++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    }
    int at = 0;
    if (negative && at < 23) display[at++] = '-';
    while (ti > 0 && at < 23)
        display[at++] = tmp[--ti];
    display[at] = '\0';
    display_len = at;
}

static int64_t parse_display(void) {
    int64_t value = 0;
    bool negative = false;
    int i = 0;
    if (display[0] == '-') {
        negative = true;
        i = 1;
    }
    for (; display[i] >= '0' && display[i] <= '9'; i++) {
        value = value * 10 + (display[i] - '0');
        if (value > 999999999LL) break;
    }
    return negative ? -value : value;
}

static bool apply_op(int64_t left, int64_t right, char op, int64_t *out) {
    if (op == '+') *out = left + right;
    else if (op == '-') *out = left - right;
    else if (op == '*') *out = left * right;
    else if (op == '/') {
        if (right == 0) return false;
        *out = left / right;
    } else {
        return false;
    }
    return true;
}

static void calc_clear(void) {
    accumulator = 0;
    pending_op = 0;
    entering = false;
    calc_error = false;
    set_display(0);
}

static void calc_memory_add(void) {
    int64_t value = entering ? parse_display() : accumulator;
    memory += value;
    entering = false;
    set_display(memory);
}

static void calc_memory_recall(void) {
    if (calc_error) calc_clear();
    set_display(memory);
    entering = true;
}

static void calc_percent(void) {
    if (calc_error) return;
    int64_t value = entering ? parse_display() : 0;
    if (pending_op && accumulator != 0)
        value = accumulator * value / 100;
    else if (accumulator != 0)
        value = accumulator * value / 100;
    set_display(value);
    entering = true;
}

static void calc_digit(char digit) {
    if (calc_error) calc_clear();
    if (!entering) {
        display[0] = '0';
        display[1] = '\0';
        display_len = 1;
        entering = true;
    }
    if (display_len >= 18) return;
    if (!(display_len == 1 && display[0] == '0')) {
        display[display_len++] = digit;
        display[display_len] = '\0';
    } else {
        display[0] = digit;
    }
}

static void calc_operator(char op) {
    if (calc_error) return;
    int64_t right = entering ? parse_display() : accumulator;
    if (pending_op) {
        int64_t result = 0;
        if (!apply_op(accumulator, right, pending_op, &result)) {
            calc_error = true;
            pending_op = 0;
            entering = false;
            copy_text(display, "Error", sizeof(display));
            display_len = 5;
            return;
        }
        accumulator = result;
    } else {
        accumulator = right;
    }
    pending_op = op;
    entering = false;
    set_display(accumulator);
}

static void calc_equals(void) {
    if (calc_error) return;
    if (!pending_op) {
        if (entering) accumulator = parse_display();
        set_display(accumulator);
        entering = false;
        return;
    }
    int64_t right = entering ? parse_display() : accumulator;
    int64_t result = 0;
    if (!apply_op(accumulator, right, pending_op, &result)) {
        calc_error = true;
        pending_op = 0;
        entering = false;
        copy_text(display, "Error", sizeof(display));
        display_len = 5;
        return;
    }
    pending_op = 0;
    accumulator = result;
    entering = false;
    set_display(accumulator);
}

static void calc_backspace(void) {
    if (calc_error) {
        calc_clear();
        return;
    }
    if (!entering || display_len <= 1) {
        display[0] = '0';
        display[1] = '\0';
        display_len = 1;
        entering = false;
        return;
    }
    display[--display_len] = '\0';
}

static void calc_input(char key) {
    if (key >= '0' && key <= '9') {
        calc_digit(key);
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        calc_operator(key);
    } else if (key == '=' || key == '\n' || key == '\r') {
        calc_equals();
    } else if (key == 'c' || key == 'C') {
        calc_clear();
    } else if (key == '\b' || key == 0x7F) {
        calc_backspace();
    } else if (key == 'm' || key == 'M') {
        calc_memory_add();
    } else if (key == 'r' || key == 'R') {
        calc_memory_recall();
    } else if (key == '%') {
        calc_percent();
    }
}

static void button_rect(int x, int y, int w, int h, int row, int col,
                        int *bx, int *by, int *bw, int *bh) {
    int grid_w = w - 24;
    int grid_h = h - GRID_TOP - 12;
    *bw = (grid_w - (GRID_COLS - 1) * GRID_GAP) / GRID_COLS;
    *bh = (grid_h - (GRID_ROWS - 1) * GRID_GAP) / GRID_ROWS;
    *bx = x + 12 + col * (*bw + GRID_GAP);
    *by = y + GRID_TOP + row * (*bh + GRID_GAP);
}

void calculator_init(void) {
    memory = 0;
    calc_clear();
}

void calculator_draw(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, h, 0xFF141C2B);
    graphics_fill_rect(x, y, w, 30, 0xFF1D2940);
    graphics_draw_string(x + 12, y + 7, "Calculator", 0xFFEAF2FF);
    graphics_draw_string(x + w - 58, y + 7, "SOLIS", 0xFF6E8CB5);

    graphics_fill_rect(x + 12, y + 38, w - 24, 58, 0xFF0B1220);
    graphics_draw_rect(x + 12, y + 38, w - 24, 58, 0xFF385A8C);
    int dl = text_len(display);
    int dx = x + w - 22 - dl * 8;
    if (dx < x + 20) dx = x + 20;
    graphics_draw_string(dx, y + 59, display, 0xFFF5FAFF);
    if (memory != 0) graphics_draw_string(x + 18, y + 40, "M", 0xFFE4B363);

    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int bx, by, bw, bh;
            button_rect(x, y, w, h, row, col, &bx, &by, &bw, &bh);
            const char *label = keys[row][col];
            uint32_t fill = 0xFF26344C;
            if (label[0] == '=') fill = 0xFF4A90E2;
            else if (label[0] == 'C') fill = 0xFF8E3F48;
            else if (label[0] == '+' || label[0] == '-' ||
                     label[0] == '*' || label[0] == '/') fill = 0xFF354E72;
            graphics_fill_rect(bx, by, bw, bh, fill);
            graphics_draw_rect(bx, by, bw, bh, 0xFF4B6183);
            int lx = bx + (bw - text_len(label) * 8) / 2;
            int ly = by + (bh - 16) / 2;
            graphics_draw_string(lx, ly, label, 0xFFFFFFFF);
        }
    }
}

void calculator_handle_key(char key) {
    calc_input(key);
}

void calculator_handle_mouse(int x, int y, int w, int h, int mx, int my) {
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int bx, by, bw, bh;
            button_rect(x, y, w, h, row, col, &bx, &by, &bw, &bh);
            if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                calc_input(keys[row][col][0]);
                return;
            }
        }
    }
}

void calculator_get_size(int *w, int *h) {
    if (w) *w = CALC_W;
    if (h) *h = CALC_H;
}

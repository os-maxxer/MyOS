#include <solis/apps/paint.h>
#include <solis/graphics.h>
#include <solis/gui.h>
#include <solis/vfs.h>
#include <stdbool.h>

#define CANVAS_W 640
#define CANVAS_H 400
#define TOOLBAR_H 46
#define STATUS_H 20
#define PALETTE_COUNT 8

enum paint_tool {
    TOOL_BRUSH = 0,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_ERASER,
};

static uint32_t canvas[CANVAS_H][CANVAS_W];
static uint32_t undo_canvas[CANVAS_H][CANVAS_W];
static bool undo_valid = false;
static uint32_t paint_color = 0xFF0000FF;
static enum paint_tool paint_tool = TOOL_BRUSH;
static int shape_start_x = -1;
static int shape_start_y = -1;
static char paint_status[48] = "Brush";

static const uint32_t palette[PALETTE_COUNT] = {
    0xFF0000FF, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00,
    0xFFFFFFFF, 0xFF000000, 0xFFFF8800, 0xFF8000FF,
};

static void paint_copy_text(char *dst, const char *src, int max) {
    int i = 0;
    for (; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void paint_set_status(const char *text) {
    paint_copy_text(paint_status, text, sizeof(paint_status));
}

static void paint_push_undo(void) {
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            undo_canvas[y][x] = canvas[y][x];
    undo_valid = true;
}

void paint_init(void) {
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            canvas[y][x] = 0xFFFFFFFF;
    undo_valid = false;
    paint_tool = TOOL_BRUSH;
    paint_color = 0xFF0000FF;
    shape_start_x = -1;
    shape_start_y = -1;
    paint_set_status("Brush");
}

static void paint_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        canvas[y][x] = color;
}

static void paint_brush(int cx, int cy) {
    uint32_t color = paint_tool == TOOL_ERASER ? 0xFFFFFFFF : paint_color;
    for (int y = -2; y <= 2; y++)
        for (int x = -2; x <= 2; x++) {
            if (x * x + y * y <= 5)
                paint_pixel(cx + x, cy + y, color);
        }
}

static void paint_line(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        paint_pixel(x0, y0, paint_color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void paint_rectangle(int x0, int y0, int x1, int y1) {
    int left = x0 < x1 ? x0 : x1;
    int right = x0 > x1 ? x0 : x1;
    int top = y0 < y1 ? y0 : y1;
    int bottom = y0 > y1 ? y0 : y1;
    for (int x = left; x <= right; x++) {
        paint_pixel(x, top, paint_color);
        paint_pixel(x, bottom, paint_color);
    }
    for (int y = top; y <= bottom; y++) {
        paint_pixel(left, y, paint_color);
        paint_pixel(right, y, paint_color);
    }
}

/* Save the canvas as a simple binary image:
 * 8-byte header "SPB1" + w(le16) + h(le16), then BGR pixels row-major. */
static void paint_save(void) {
    char path[64];
    if (!gui_save_dialog("Image", path, 64)) {
        paint_set_status("Save cancelled");
        return;
    }
    int fd = vfs_create(path);
    if (fd < 0) { fd = vfs_open(path); if (fd < 0) { paint_set_status("Cannot save"); return; } }

    static uint8_t img[CANVAS_W * CANVAS_H * 3 + 8];
    img[0] = 'S'; img[1] = 'P'; img[2] = 'B'; img[3] = 1;
    img[4] = (uint8_t)(CANVAS_W & 0xFF);
    img[5] = (uint8_t)((CANVAS_W >> 8) & 0xFF);
    img[6] = (uint8_t)(CANVAS_H & 0xFF);
    img[7] = (uint8_t)((CANVAS_H >> 8) & 0xFF);
    uint32_t o = 8;
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            uint32_t c = canvas[y][x];
            img[o++] = (uint8_t)(c & 0xFF);
            img[o++] = (uint8_t)((c >> 8) & 0xFF);
            img[o++] = (uint8_t)((c >> 16) & 0xFF);
        }
    }
    if (vfs_write(fd, img, o) < 0) paint_set_status("Write failed");
    else paint_set_status("Saved");
}

static void paint_clear_canvas(void) {
    paint_push_undo();
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            canvas[y][x] = 0xFFFFFFFF;
    shape_start_x = -1;
    paint_set_status("Cleared");
}

static void paint_draw_toolbar(int x, int y, int w) {
    graphics_fill_rect(x, y, w, TOOLBAR_H, 0xFFE8E8E8);
    graphics_draw_rect(x, y, w, TOOLBAR_H, 0xFFCCCCCC);

    graphics_fill_rect(x + 4, y + 3, 48, 20, 0xFF3B8B3B);
    graphics_draw_string(x + 10, y + 6, "Save", 0xFFFFFFFF);
    graphics_fill_rect(x + 58, y + 3, 50, 20, 0xFF8E3F48);
    graphics_draw_string(x + 64, y + 6, "Clear", 0xFFFFFFFF);

    const char *tools[] = {"Pen", "Line", "Rect", "Erase"};
    for (int i = 0; i < 4; i++) {
        int bx = x + 116 + i * 42;
        uint32_t fill = (paint_tool == (enum paint_tool)i) ? 0xFF4A90E2 : 0xFFD0D0D0;
        graphics_fill_rect(bx, y + 3, 38, 20, fill);
        graphics_draw_string(bx + 3, y + 6, tools[i], (paint_tool == (enum paint_tool)i) ? 0xFFFFFFFF : 0xFF333333);
    }

    for (int i = 0; i < PALETTE_COUNT; i++) {
        int sx = x + 4 + i * 22;
        graphics_fill_rect(sx, y + 27, 18, 14, palette[i]);
        graphics_draw_rect(sx, y + 27, 18, 14,
                           palette[i] == paint_color ? 0xFF4A90E2 : 0xFF777777);
    }
    graphics_draw_string(x + 186, y + 28, paint_status, 0xFF666666);
}

void paint_draw(int x, int y, int w, int h) {
    paint_draw_toolbar(x, y, w);

    int draw_w = CANVAS_W;
    int draw_h = CANVAS_H;
    if (draw_w > w - 8) draw_w = w - 8;
    if (draw_h > h - TOOLBAR_H - STATUS_H - 6) draw_h = h - TOOLBAR_H - STATUS_H - 6;
    if (draw_w < 0) draw_w = 0;
    if (draw_h < 0) draw_h = 0;

    int ox = x + 4;
    int oy = y + TOOLBAR_H + 2;
    for (int py = 0; py < draw_h; py++) {
        for (int px = 0; px < draw_w; px++) {
            graphics_put_pixel(ox + px, oy + py, canvas[py][px]);
        }
    }
    graphics_draw_rect(ox, oy, draw_w, draw_h, 0xFF999999);

    if (shape_start_x >= 0) {
        char start[32];
        int n = 0;
        const char *prefix = "Start ";
        while (*prefix) start[n++] = *prefix++;
        start[n++] = '0' + (shape_start_x / 100) % 10;
        start[n++] = '0' + (shape_start_x / 10) % 10;
        start[n++] = '0' + shape_start_x % 10;
        start[n++] = ',';
        start[n++] = '0' + (shape_start_y / 100) % 10;
        start[n++] = '0' + (shape_start_y / 10) % 10;
        start[n++] = '0' + shape_start_y % 10;
        start[n] = '\0';
        graphics_draw_string(x + 186, y + 28, start, 0xFF666666);
    } else {
        graphics_draw_string(x + 186, y + 28, paint_status, 0xFF666666);
    }

    graphics_fill_rect(x, y + h - STATUS_H, w, STATUS_H, 0xFFE8E8E8);
    graphics_draw_string(x + 4, y + h - 16,
                         "1 Pen  2 Line  3 Rect  4 Erase  |  B R G Y W palette  |  Ctrl+Z undo", 0xFF333333);
}

void paint_handle_key(char key) {
    if (key == 0x13) { paint_save(); return; }
    if (key == 0x1A) {
        if (!undo_valid) { paint_set_status("Nothing to undo"); return; }
        for (int y = 0; y < CANVAS_H; y++)
            for (int x = 0; x < CANVAS_W; x++)
                canvas[y][x] = undo_canvas[y][x];
        undo_valid = false;
        shape_start_x = -1;
        paint_set_status("Undo");
        return;
    }
    if (key == '1') { paint_tool = TOOL_BRUSH; paint_set_status("Brush"); }
    else if (key == '2') { paint_tool = TOOL_LINE; paint_set_status("Line"); }
    else if (key == '3') { paint_tool = TOOL_RECT; paint_set_status("Rectangle"); }
    else if (key == '4') { paint_tool = TOOL_ERASER; paint_set_status("Eraser"); }
    else if (key == 'b' || key == 'B') paint_color = 0xFF0000FF;
    else if (key == 'r' || key == 'R') paint_color = 0xFFFF0000;
    else if (key == 'g' || key == 'G') paint_color = 0xFF00FF00;
    else if (key == 'y' || key == 'Y') paint_color = 0xFFFFFF00;
    else if (key == 'w' || key == 'W') paint_color = 0xFFFFFFFF;
    else if (key == 'c' || key == 'C') paint_clear_canvas();
    else if (key == 0x1B) { shape_start_x = -1; paint_set_status("Cancelled"); }
}

void paint_handle_mouse(int win_x, int win_y, int win_w, int win_h, int mouse_x, int mouse_y) {
    int rel_x = mouse_x - win_x;
    int rel_y = mouse_y - win_y;

    if (rel_y >= 0 && rel_y < TOOLBAR_H) {
        if (rel_x >= 4 && rel_x < 52) { paint_save(); return; }
        if (rel_x >= 58 && rel_x < 108) { paint_clear_canvas(); return; }
        for (int i = 0; i < 4; i++) {
            int bx = 116 + i * 42;
            if (rel_x >= bx && rel_x < bx + 38) {
                paint_tool = (enum paint_tool)i;
                paint_set_status(i == 0 ? "Brush" : i == 1 ? "Line" : i == 2 ? "Rectangle" : "Eraser");
                shape_start_x = -1;
                return;
            }
        }
        for (int i = 0; i < PALETTE_COUNT; i++) {
            int sx = 4 + i * 22;
            if (rel_x >= sx && rel_x < sx + 18) {
                paint_color = palette[i];
                return;
            }
        }
        return;
    }

    int draw_w = CANVAS_W;
    int draw_h = CANVAS_H;
    if (draw_w > win_w - 8) draw_w = win_w - 8;
    if (draw_h > win_h - TOOLBAR_H - STATUS_H - 6) draw_h = win_h - TOOLBAR_H - STATUS_H - 6;
    int cx = rel_x - 4;
    int cy = rel_y - TOOLBAR_H - 2;
    if (cx < 0 || cy < 0 || cx >= draw_w || cy >= draw_h) return;
    if (cx >= CANVAS_W || cy >= CANVAS_H) return;

    if (paint_tool == TOOL_BRUSH || paint_tool == TOOL_ERASER) {
        paint_push_undo();
        paint_brush(cx, cy);
        paint_set_status(paint_tool == TOOL_ERASER ? "Erased" : "Drawn");
        return;
    }

    if (shape_start_x < 0) {
        paint_push_undo();
        shape_start_x = cx;
        shape_start_y = cy;
        paint_set_status("Click again to finish");
        return;
    }

    if (paint_tool == TOOL_LINE) paint_line(shape_start_x, shape_start_y, cx, cy);
    else paint_rectangle(shape_start_x, shape_start_y, cx, cy);
    shape_start_x = -1;
    paint_set_status("Shape drawn");
}

#include <solis/apps/paint.h>
#include <solis/graphics.h>
#include <solis/gui.h>
#include <solis/vfs.h>

#define CANVAS_W 640
#define CANVAS_H 400
#define TOOLBAR_H 28

static uint32_t canvas[CANVAS_H][CANVAS_W];
static uint32_t paint_color = 0xFF0000FF;

void paint_init(void) {
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            canvas[y][x] = 0xFFFFFFFF;
}

/* Save the canvas as a simple binary image:
 * 8-byte header "SPB1" + w(le16) + h(le16), then BGR pixels row-major. */
static void paint_save(void) {
    char path[64];
    if (!gui_save_dialog("Image", path, 64)) return;

    int fd = vfs_create(path);
    if (fd < 0) { fd = vfs_open(path); if (fd < 0) return; }

    uint8_t hdr[8];
    hdr[0] = 'S'; hdr[1] = 'P'; hdr[2] = 'B'; hdr[3] = 1;
    hdr[4] = (uint8_t)(CANVAS_W & 0xFF);
    hdr[5] = (uint8_t)((CANVAS_W >> 8) & 0xFF);
    hdr[6] = (uint8_t)(CANVAS_H & 0xFF);
    hdr[7] = (uint8_t)((CANVAS_H >> 8) & 0xFF);
    vfs_write(fd, hdr, 8);

    for (int y = 0; y < CANVAS_H; y++) {
        uint8_t row[CANVAS_W * 3];
        for (int x = 0; x < CANVAS_W; x++) {
            uint32_t c = canvas[y][x];
            row[x * 3 + 0] = (uint8_t)(c & 0xFF);
            row[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
        }
        vfs_write(fd, row, CANVAS_W * 3);
    }
}

void paint_draw(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, TOOLBAR_H, 0xFFE8E8E8);
    graphics_draw_rect(x, y, w, TOOLBAR_H, 0xFFCCCCCC);

    graphics_fill_rect(x + 4, y + 3, 54, 22, 0xFF3B8B3B);
    graphics_draw_string(x + 10, y + 7, "Save", 0xFFFFFFFF);

    int draw_w = CANVAS_W;
    int draw_h = CANVAS_H;
    if (draw_w > w - 4) draw_w = w - 4;
    if (draw_h > h - TOOLBAR_H - 4) draw_h = h - TOOLBAR_H - 4;

    for (int py = 0; py < draw_h; py++) {
        for (int px = 0; px < draw_w; px++) {
            graphics_put_pixel(x + 4 + px, y + TOOLBAR_H + 4 + py, canvas[py][px]);
        }
    }

    graphics_draw_string(x + 4, y + TOOLBAR_H + draw_h + 8,
                         "Ctrl+S Save  |  [B]lue [R]ed [G]reen [Y]ellow [W]hite  |  [C]lear",
                         0xFF222222);
}

void paint_handle_key(char key) {
    if (key == 0x13) { paint_save(); return; }
    if (key == 'b' || key == 'B') paint_color = 0xFF0000FF;
    else if (key == 'r' || key == 'R') paint_color = 0xFFFF0000;
    else if (key == 'g' || key == 'G') paint_color = 0xFF00FF00;
    else if (key == 'y' || key == 'Y') paint_color = 0xFFFFFF00;
    else if (key == 'w' || key == 'W') paint_color = 0xFFFFFFFF;
    else if (key == 'c' || key == 'C') {
        paint_init();
    }
}

void paint_handle_mouse(int win_x, int win_y, int win_w, int win_h, int mouse_x, int mouse_y) {
    (void)win_w;
    (void)win_h;
    int rel_x = mouse_x - win_x;
    int rel_y = mouse_y - win_y;

    if (rel_y >= 3 && rel_y < 25) {
        if (rel_x >= 4 && rel_x < 58) paint_save();
    }
}

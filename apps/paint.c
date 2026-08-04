#include <solis/apps/paint.h>
#include <solis/graphics.h>

#define CANVAS_W 640
#define CANVAS_H 400

static uint32_t canvas[CANVAS_H][CANVAS_W];
static uint32_t paint_color = 0xFF0000FF;

void paint_init(void) {
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++)
            canvas[y][x] = 0xFFFFFFFF;
}

void paint_draw(int x, int y, int w, int h) {
    (void)w;
    (void)h;
    int draw_w = CANVAS_W;
    int draw_h = CANVAS_H;
    if (draw_w > w - 4) draw_w = w - 4;
    if (draw_h > h - 4) draw_h = h - 4;

    for (int py = 0; py < draw_h; py++) {
        for (int px = 0; px < draw_w; px++) {
            graphics_put_pixel(x + 4 + px, y + 4 + py, canvas[py][px]);
        }
    }

    graphics_draw_string(x + 4, y + draw_h + 8, "C: [B]lue [R]ed [G]reen [Y]ellow [W]hite", 0xFF222222);
}

void paint_handle_key(char key) {
    if (key == 'b' || key == 'B') paint_color = 0xFF0000FF;
    else if (key == 'r' || key == 'R') paint_color = 0xFFFF0000;
    else if (key == 'g' || key == 'G') paint_color = 0xFF00FF00;
    else if (key == 'y' || key == 'Y') paint_color = 0xFFFFFF00;
    else if (key == 'w' || key == 'W') paint_color = 0xFFFFFFFF;
    else if (key == 'c' || key == 'C') {
        paint_init();
    }
}

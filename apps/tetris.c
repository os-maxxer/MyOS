#include <solis/apps/tetris.h>
#include <solis/graphics.h>
#include <solis/timer.h>

#define COLS 10
#define ROWS 20
#define BS 20
#define NEXT_X 14
#define NEXT_Y 2

static int grid[ROWS][COLS];
static int score;
static int level;
static int lines_cleared;
static int game_over;
static int paused;

static int current_type;
static int current_x;
static int current_y;
static int current_rot;

static int next_type;

static int drop_counter;
static int drop_interval;

static const int shapes[7][4][4] = {
    /* I */ {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
    /* O */ {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
    /* T */ {{0,0,0,0},{0,1,0,0},{1,1,1,0},{0,0,0,0}},
    /* S */ {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
    /* Z */ {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
    /* J */ {{0,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,0}},
    /* L */ {{0,0,0,0},{0,0,1,0},{1,1,1,0},{0,0,0,0}},
};

static uint32_t colors[7] = {
    0xFF00FFFF, 0xFFFFFF00, 0xFFAA00FF,
    0xFF00FF00, 0xFFFF0000, 0xFF0000FF, 0xFFFF8800,
};

static int prng_state = 1;

static int rand_int(int max) {
    prng_state = prng_state * 1103515245 + 12345;
    return (prng_state >> 16) % max;
}

static int get_block(int type, int rot, int r, int c) {
    int pr = r, pc = c;
    for (int i = 0; i < (rot & 3); i++) {
        int tmp = pr;
        pr = pc;
        pc = 3 - tmp;
    }
    return shapes[type][pr][pc];
}

static void rotate_cw(void) {
    int new_rot = (current_rot + 1) % 4;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (get_block(current_type, new_rot, r, c)) {
                int nx = current_x + c;
                int ny = current_y + r;
                if (nx < 0 || nx >= COLS || ny >= ROWS) return;
                if (ny >= 0 && grid[ny][nx]) return;
            }
        }
    }
    current_rot = new_rot;
}

static int collides(int type, int rot, int x, int y) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (get_block(type, rot, r, c)) {
                int nx = x + c;
                int ny = y + r;
                if (nx < 0 || nx >= COLS || ny >= ROWS) return 1;
                if (ny >= 0 && grid[ny][nx]) return 1;
            }
        }
    }
    return 0;
}

static void lock_piece(void) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (get_block(current_type, current_rot, r, c)) {
                int nx = current_x + c;
                int ny = current_y + r;
                if (ny >= 0 && ny < ROWS && nx >= 0 && nx < COLS)
                    grid[ny][nx] = current_type + 1;
            }
        }
    }
}

static void clear_lines(void) {
    int cleared = 0;
    for (int r = ROWS - 1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < COLS; c++)
            if (!grid[r][c]) { full = 0; break; }
        if (full) {
            for (int r2 = r; r2 > 0; r2--)
                for (int c2 = 0; c2 < COLS; c2++)
                    grid[r2][c2] = grid[r2 - 1][c2];
            for (int c2 = 0; c2 < COLS; c2++)
                grid[0][c2] = 0;
            cleared++;
            r++;
        }
    }
    if (cleared) {
        lines_cleared += cleared;
        score += cleared * 100 * (level + 1);
        level = lines_cleared / 5;
        drop_interval = 20 - level;
        if (drop_interval < 2) drop_interval = 2;
    }
}

static void spawn_piece(void) {
    current_type = next_type;
    current_x = COLS / 2 - 2;
    current_y = 0;
    current_rot = 0;
    next_type = rand_int(7);
    if (collides(current_type, current_rot, current_x, current_y))
        game_over = 1;
}

static void hard_drop(void) {
    while (!collides(current_type, current_rot, current_x, current_y + 1))
        current_y++;
    lock_piece();
    clear_lines();
    spawn_piece();
}

static void move_left(void) {
    if (!collides(current_type, current_rot, current_x - 1, current_y))
        current_x--;
}

static void move_right(void) {
    if (!collides(current_type, current_rot, current_x + 1, current_y))
        current_x++;
}

static void move_down(void) {
    if (!collides(current_type, current_rot, current_x, current_y + 1)) {
        current_y++;
    } else {
        lock_piece();
        clear_lines();
        spawn_piece();
    }
}

void tetris_init(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            grid[r][c] = 0;
    score = 0; level = 0; lines_cleared = 0;
    game_over = 0; paused = 0;
    drop_interval = 25;
    drop_counter = 0;
    current_type = rand_int(7);
    next_type = rand_int(7);
    current_x = COLS / 2 - 2;
    current_y = 0;
    current_rot = 0;
}

static void draw_block(int bx, int by, uint32_t color, int shade) {
    if (shade) {
        graphics_fill_rect(bx + 1, by + 1, BS - 2, BS - 2, color);
        graphics_draw_string(bx + 1, by + 1, " ", 0x00000000);
    } else {
        graphics_fill_rect(bx, by, BS, BS, color);
    }
    graphics_draw_rect(bx, by, BS, BS, 0xFF333333);
}

void tetris_draw(int wx, int wy, int w, int h) {
    uint32_t bg = 0xFF1A1A2E;
    graphics_fill_rect(wx, wy, w, h, bg);

    int grid_x = wx + (w - COLS * BS) / 2;
    int grid_y = wy + (h - ROWS * BS) / 2;

    /* Draw grid background */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int bx = grid_x + c * BS;
            int by = grid_y + r * BS;
            uint32_t cell_color = 0xFF0D0D1B;
            if (grid[r][c])
                cell_color = colors[grid[r][c] - 1];
            draw_block(bx, by, cell_color, grid[r][c] != 0);
        }
    }

    /* Draw grid lines */
    for (int c = 0; c <= COLS; c++) {
        int lx = grid_x + c * BS;
        graphics_fill_rect(lx, grid_y, 1, ROWS * BS, 0xFF2A2A4A);
    }
    for (int r = 0; r <= ROWS; r++) {
        int ly = grid_y + r * BS;
        graphics_fill_rect(grid_x, ly, COLS * BS, 1, 0xFF2A2A4A);
    }

    /* Auto-drop */
    if (!game_over && !paused) {
        drop_counter++;
        if (drop_counter >= drop_interval) {
            drop_counter = 0;
            if (!collides(current_type, current_rot, current_x, current_y + 1)) {
                current_y++;
            } else {
                lock_piece();
                clear_lines();
                spawn_piece();
            }
        }
    }

    /* Draw current piece */
    if (!game_over && !paused) {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (get_block(current_type, current_rot, r, c)) {
                    int bx = grid_x + (current_x + c) * BS;
                    int by = grid_y + (current_y + r) * BS;
                    draw_block(bx, by, colors[current_type], 1);
                }
            }
        }
    }

    /* Draw ghost piece (shadow) */
    if (!game_over && !paused) {
        int ghost_y = current_y;
        while (!collides(current_type, current_rot, current_x, ghost_y + 1))
            ghost_y++;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (get_block(current_type, current_rot, r, c)) {
                    int bx = grid_x + (current_x + c) * BS;
                    int by = grid_y + (ghost_y + r) * BS;
                    uint32_t ghost_c = colors[current_type] & 0x44FFFFFF;
                    graphics_draw_rect(bx, by, BS, BS, ghost_c);
                }
            }
        }
    }

    /* Side panel */
    int panel_x = grid_x + COLS * BS + 10;
    int panel_y = grid_y;

    graphics_draw_string(panel_x, panel_y, "NEXT", 0xFFFFFFFF);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = panel_x + c * (BS - 4) + 4;
            int by = panel_y + 20 + r * (BS - 4);
            if (get_block(next_type, 0, r, c)) {
                graphics_fill_rect(bx, by, BS - 4, BS - 4, colors[next_type]);
                graphics_draw_rect(bx, by, BS - 4, BS - 4, 0xFF888888);
            }
        }
    }

    /* Score info */
    char buf[32];
    int bi;

    graphics_draw_string(panel_x, panel_y + 110, "SCORE", 0xFFAAAAAA);
    bi = 0;
    int s = score;
    if (s == 0) { buf[bi++] = '0'; }
    while (s > 0) { buf[bi++] = '0' + (s % 10); s /= 10; }
    buf[bi] = '\0';
    for (int i = 0; i < bi / 2; i++) { char t = buf[i]; buf[i] = buf[bi-1-i]; buf[bi-1-i] = t; }
    graphics_draw_string(panel_x, panel_y + 128, buf, 0xFFFFFFFF);

    graphics_draw_string(panel_x, panel_y + 150, "LEVEL", 0xFFAAAAAA);
    bi = 0; s = level;
    if (s == 0) { buf[bi++] = '0'; }
    while (s > 0) { buf[bi++] = '0' + (s % 10); s /= 10; }
    buf[bi] = '\0';
    for (int i = 0; i < bi / 2; i++) { char t = buf[i]; buf[i] = buf[bi-1-i]; buf[bi-1-i] = t; }
    graphics_draw_string(panel_x, panel_y + 168, buf, 0xFFFFFFFF);

    graphics_draw_string(panel_x, panel_y + 190, "LINES", 0xFFAAAAAA);
    bi = 0; s = lines_cleared;
    if (s == 0) { buf[bi++] = '0'; }
    while (s > 0) { buf[bi++] = '0' + (s % 10); s /= 10; }
    buf[bi] = '\0';
    for (int i = 0; i < bi / 2; i++) { char t = buf[i]; buf[i] = buf[bi-1-i]; buf[bi-1-i] = t; }
    graphics_draw_string(panel_x, panel_y + 208, buf, 0xFFFFFFFF);

    /* Controls */
    graphics_draw_string(panel_x, panel_y + 240, "WASD/Space", 0xFF888888);
    graphics_draw_string(panel_x, panel_y + 256, "P=pause  R=restart", 0xFF888888);

    /* Game over overlay */
    if (game_over) {
        int ox = wx + w / 2 - 80;
        int oy = wy + h / 2 - 30;
        graphics_fill_rect(ox, oy, 160, 60, 0xCC000000);
        graphics_draw_rect(ox, oy, 160, 60, 0xFFFF0000);
        graphics_draw_string(ox + 20, oy + 12, "GAME OVER", 0xFFFF4444);
        graphics_draw_string(ox + 8, oy + 34, "Press R to restart", 0xFFAAAAAA);
    }

    /* Pause overlay */
    if (paused && !game_over) {
        int ox = wx + w / 2 - 60;
        int oy = wy + h / 2 - 20;
        graphics_fill_rect(ox, oy, 120, 40, 0xCC000000);
        graphics_draw_rect(ox, oy, 120, 40, 0xFF888888);
        graphics_draw_string(ox + 30, oy + 14, "PAUSED", 0xFFFFAA00);
    }
}

void tetris_handle_key(char key) {
    if (key == 'r' || key == 'R') {
        tetris_init();
        return;
    }

    if (game_over) return;

    if (key == 'p' || key == 'P') {
        paused = !paused;
        return;
    }
    if (paused) return;

    switch (key) {
        case 'a': case 'A': move_left(); break;
        case 'd': case 'D': move_right(); break;
        case 's': case 'S': move_down(); break;
        case 'w': case 'W': rotate_cw(); break;
        case ' ': hard_drop(); break;
    }
}

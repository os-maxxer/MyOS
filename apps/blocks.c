#include <solis/apps/blocks.h>
#include <solis/graphics.h>

#define WORLD_SIZE 16
#define WORLD_HEIGHT 8
#define FIXED_ONE 256
#define RAY_MAX_STEPS 48

static uint8_t world[WORLD_HEIGHT][WORLD_SIZE][WORLD_SIZE];
static int32_t player_x;
static int32_t player_y;
static int32_t camera_z;
static int player_cell_x;
static int player_cell_y;
static int selected_material;
static int view_x;
static int view_y;
static int view_width;
static int view_height;
static int yaw;

static const int direction_x[8] = {256, 181, 0, -181, -256, -181, 0, 181};
static const int direction_y[8] = {0, 181, 256, 181, 0, -181, -256, -181};
static const uint32_t block_colors[6] = {
    0xFF000000, 0xFF88C76E, 0xFF9B704E, 0xFF929BA8, 0xFFB77A48, 0xFF5DA35D,
};
static const char *material_names[3] = {"GRASS", "STONE", "WOOD"};

struct ray_hit {
    int x;
    int y;
    int z;
    int previous_x;
    int previous_y;
    int previous_z;
    int face;
    int distance;
};

static int abs_i(int value) {
    return value < 0 ? -value : value;
}

static int cast_ray(int ray_x, int ray_y, int ray_z, struct ray_hit *hit) {
    int cell_x = player_x / FIXED_ONE;
    int cell_y = player_y / FIXED_ONE;
    int cell_z = camera_z / FIXED_ONE;
    int step_x = ray_x >= 0 ? 1 : -1;
    int step_y = ray_y >= 0 ? 1 : -1;
    int step_z = ray_z >= 0 ? 1 : -1;
    int abs_x = abs_i(ray_x), abs_y = abs_i(ray_y), abs_z = abs_i(ray_z);
    int edge_x = step_x > 0 ? (cell_x + 1) * FIXED_ONE - player_x
                            : player_x - cell_x * FIXED_ONE;
    int edge_y = step_y > 0 ? (cell_y + 1) * FIXED_ONE - player_y
                            : player_y - cell_y * FIXED_ONE;
    int edge_z = step_z > 0 ? (cell_z + 1) * FIXED_ONE - camera_z
                            : camera_z - cell_z * FIXED_ONE;
    int max_x = abs_x ? edge_x * FIXED_ONE / abs_x : 0x3FFFFFFF;
    int max_y = abs_y ? edge_y * FIXED_ONE / abs_y : 0x3FFFFFFF;
    int max_z = abs_z ? edge_z * FIXED_ONE / abs_z : 0x3FFFFFFF;
    int delta_x = abs_x ? FIXED_ONE * FIXED_ONE / abs_x : 0x3FFFFFFF;
    int delta_y = abs_y ? FIXED_ONE * FIXED_ONE / abs_y : 0x3FFFFFFF;
    int delta_z = abs_z ? FIXED_ONE * FIXED_ONE / abs_z : 0x3FFFFFFF;
    int distance = 0;

    for (int step = 0; step < RAY_MAX_STEPS; step++) {
        int previous_x = cell_x;
        int previous_y = cell_y;
        int previous_z = cell_z;
        int face;
        if (max_x <= max_y && max_x <= max_z) {
            distance = max_x;
            max_x += delta_x;
            cell_x += step_x;
            face = 0;
        } else if (max_y <= max_z) {
            distance = max_y;
            max_y += delta_y;
            cell_y += step_y;
            face = 1;
        } else {
            distance = max_z;
            max_z += delta_z;
            cell_z += step_z;
            face = 2;
        }

        if (cell_x < 0 || cell_x >= WORLD_SIZE || cell_y < 0 || cell_y >= WORLD_SIZE ||
            cell_z < 0 || cell_z >= WORLD_HEIGHT || distance > 24 * FIXED_ONE)
            return 0;

        if (world[cell_z][cell_y][cell_x] != 0) {
            hit->x = cell_x;
            hit->y = cell_y;
            hit->z = cell_z;
            hit->previous_x = previous_x;
            hit->previous_y = previous_y;
            hit->previous_z = previous_z;
            hit->face = face;
            hit->distance = distance;
            return 1;
        }
    }
    return 0;
}

static uint32_t shade_block(const struct ray_hit *hit) {
    uint32_t color = block_colors[world[hit->z][hit->y][hit->x]];
    int light = hit->face == 2 ? 34 : (hit->face == 0 ? 25 : 20);
    int distance_shade = hit->distance / (FIXED_ONE * 2);
    if (distance_shade > 17) distance_shade = 17;
    light -= distance_shade;
    int texture = (hit->x * 7 + hit->y * 11 + hit->z * 3) % 5;
    light += texture - 2;
    int r = (int)((color >> 16) & 0xFF) * light / 32;
    int g = (int)((color >> 8) & 0xFF) * light / 32;
    int b = (int)(color & 0xFF) * light / 32;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void reset_world(void) {
    for (int z = 0; z < WORLD_HEIGHT; z++)
        for (int y = 0; y < WORLD_SIZE; y++)
            for (int x = 0; x < WORLD_SIZE; x++)
                world[z][y][x] = 0;

    for (int y = 0; y < WORLD_SIZE; y++) {
        for (int x = 0; x < WORLD_SIZE; x++) {
            world[0][y][x] = 3;
            world[1][y][x] = 1;
            int dx = x - WORLD_SIZE / 2;
            int dy = y - WORLD_SIZE / 2;
            int distance = abs_i(dx) + abs_i(dy);
            if (distance > 2 && (x * 7 + y * 11) % 19 == 0)
                world[2][y][x] = 3;
            if (distance > 4 && (x * 5 + y * 13) % 37 == 0)
                world[3][y][x] = 3;
        }
    }

    static const int trees[4][2] = {{12, 8}, {4, 4}, {11, 12}, {3, 11}};
    for (int i = 0; i < 4; i++) {
        int tx = trees[i][0], ty = trees[i][1];
        world[2][ty][tx] = 4;
        world[3][ty][tx] = 4;
        world[4][ty][tx] = 5;
        for (int ly = -1; ly <= 1; ly++) {
            for (int lx = -1; lx <= 1; lx++) {
                int ax = tx + lx, ay = ty + ly;
                if ((lx || ly) && ax >= 0 && ax < WORLD_SIZE && ay >= 0 && ay < WORLD_SIZE)
                    world[4][ay][ax] = 5;
            }
        }
        world[5][ty][tx] = 5;
    }

    player_x = (WORLD_SIZE / 2) * FIXED_ONE + FIXED_ONE / 2;
    player_y = (WORLD_SIZE / 2) * FIXED_ONE + FIXED_ONE / 2;
    player_cell_x = WORLD_SIZE / 2;
    player_cell_y = WORLD_SIZE / 2;
    camera_z = 2 * FIXED_ONE + 160;
    yaw = 0;
    selected_material = 0;
}

void blocks_init(void) {
    reset_world();
}

static int ground_level(int x, int y) {
    for (int z = WORLD_HEIGHT - 1; z >= 0; z--)
        if (world[z][y][x]) return z + 1;
    return 0;
}

static void update_player_cell(void) {
    player_cell_x = player_x / FIXED_ONE;
    player_cell_y = player_y / FIXED_ONE;
    camera_z = ground_level(player_cell_x, player_cell_y) * FIXED_ONE + 160;
}

static int can_move_to(int x, int y) {
    if (x < 0 || x >= WORLD_SIZE || y < 0 || y >= WORLD_SIZE) return 0;
    int next_ground = ground_level(x, y);
    int current_ground = ground_level(player_cell_x, player_cell_y);
    if (next_ground == 0 || abs_i(next_ground - current_ground) > 1) return 0;
    if (next_ground < WORLD_HEIGHT && world[next_ground][y][x]) return 0;
    if (next_ground + 1 < WORLD_HEIGHT && world[next_ground + 1][y][x]) return 0;
    return 1;
}

static void move_player(int dx, int dy) {
    int next_x = player_x + dx;
    int next_y = player_y + dy;
    int cell_x = next_x / FIXED_ONE;
    int cell_y = next_y / FIXED_ONE;
    if (cell_x != player_cell_x || cell_y != player_cell_y) {
        if (!can_move_to(cell_x, cell_y)) return;
    }
    player_x = next_x;
    player_y = next_y;
    update_player_cell();
}

static void walk_forward(int direction) {
    int dx = direction_x[yaw] * 48 / FIXED_ONE;
    int dy = direction_y[yaw] * 48 / FIXED_ONE;
    move_player(dx * direction, dy * direction);
}

static void strafe_player(int direction) {
    int dx = direction_y[yaw] * 48 / FIXED_ONE;
    int dy = -direction_x[yaw] * 48 / FIXED_ONE;
    move_player(dx * direction, dy * direction);
}

static void turn_player(int direction) {
    yaw = (yaw + direction + 8) % 8;
}

static void place_block(void) {
    struct ray_hit hit;
    if (!cast_ray(direction_x[yaw], direction_y[yaw], 0, &hit)) return;
    int x = hit.previous_x, y = hit.previous_y, z = hit.previous_z;
    if (x < 0 || x >= WORLD_SIZE || y < 0 || y >= WORLD_SIZE ||
        z < 0 || z >= WORLD_HEIGHT || world[z][y][x]) return;
    if (x == player_cell_x && y == player_cell_y &&
        z >= camera_z / FIXED_ONE - 1 && z <= camera_z / FIXED_ONE + 1) return;
    static const uint8_t materials[3] = {1, 3, 4};
    world[z][y][x] = materials[selected_material];
}

static void mine_block(void) {
    struct ray_hit hit;
    if (cast_ray(direction_x[yaw], direction_y[yaw], 0, &hit))
        world[hit.z][hit.y][hit.x] = 0;
}

static uint32_t lerp_color(uint32_t a, uint32_t b, int step, int count) {
    if (count < 1) count = 1;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * step / count;
    int g = ag + (bg - ag) * step / count;
    int bl = ab + (bb - ab) * step / count;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t sky_floor_color(int row, int rows) {
    int horizon = rows / 2;
    if (row < horizon)
        return lerp_color(0xFF64729C, 0xFFB482A0, row, horizon);
    return lerp_color(0xFF596269, 0xFF252C37, row - horizon, rows - horizon);
}

static void draw_world(int x, int y, int w, int h) {
    int panel_w = w < 360 ? 104 : 114;
    view_x = x + 8;
    view_y = y + 30;
    view_width = w - panel_w - 30;
    view_height = h - 40;
    if (view_width < 80) view_width = 80;
    if (view_height < 60) view_height = 60;

    graphics_fill_rect(x, y, w, h, 0xFF27283D);
    graphics_draw_string(x + 10, y + 8, "SOLIS BLOCKS  /  FIRST PERSON", 0xFFF2EDF5);
    graphics_fill_rect(view_x - 1, view_y - 1, view_width + 2, view_height + 2, 0xFFD1B9D0);
    graphics_fill_rect(view_x, view_y, view_width, view_height, 0xFF353B50);

    int scale = 2;
    int scale_x = (view_width + 239) / 240;
    int scale_y = (view_height + 159) / 160;
    if (scale_x > scale) scale = scale_x;
    if (scale_y > scale) scale = scale_y;
    int ray_width = view_width / scale;
    int ray_height = view_height / scale;
    if (ray_width < 1) ray_width = 1;
    if (ray_height < 1) ray_height = 1;
    int center_row = ray_height / 2;
    int dir_x = direction_x[yaw];
    int dir_y = direction_y[yaw];
    int plane_x = -dir_y * 148 / FIXED_ONE;
    int plane_y = dir_x * 148 / FIXED_ONE;

    for (int row = 0; row < ray_height; row++) {
        int ray_z = (center_row - row) * 220 / ray_height;
        for (int col = 0; col < ray_width; col++) {
            int side = ((2 * col + 1 - ray_width) * 148) / ray_width;
            int ray_x = dir_x + plane_x * side / FIXED_ONE;
            int ray_y = dir_y + plane_y * side / FIXED_ONE;
            struct ray_hit hit;
            uint32_t color = sky_floor_color(row, ray_height);
            if (cast_ray(ray_x, ray_y, ray_z, &hit))
                color = shade_block(&hit);

            int px0 = view_x + col * view_width / ray_width;
            int px1 = view_x + (col + 1) * view_width / ray_width;
            int py0 = view_y + row * view_height / ray_height;
            int py1 = view_y + (row + 1) * view_height / ray_height;
            graphics_fill_rect(px0, py0, px1 - px0, py1 - py0, color);
        }
    }

    int cross_x = view_x + view_width / 2;
    int cross_y = view_y + view_height / 2;
    graphics_fill_rect(cross_x - 5, cross_y, 11, 1, 0xFF232333);
    graphics_fill_rect(cross_x, cross_y - 5, 1, 11, 0xFF232333);
    graphics_fill_rect(cross_x - 3, cross_y, 7, 1, 0xFFFFFFFF);
    graphics_fill_rect(cross_x, cross_y - 3, 1, 7, 0xFFFFFFFF);

    int px = view_x + view_width + 12;
    int py = view_y;
    graphics_fill_rect(px, py, panel_w, view_height, 0xFF35334F);
    graphics_draw_rect(px, py, panel_w, view_height, 0xFF9888B1);
    if (view_height < 260) {
        static const char *compact_controls[5] = {
            "W/S MOVE", "Q/E TURN", "C BLOCK", "SPACE ADD", "X MINE",
        };
        static const uint8_t compact_materials[3] = {1, 3, 4};
        graphics_fill_rect(px + 9, py + 2, 14, 14,
                           block_colors[compact_materials[selected_material]]);
        graphics_draw_string(px + 30, py + 2, material_names[selected_material], 0xFFF2EDF5);
        int compact_count = (view_height - 20) / 16;
        if (compact_count > 5) compact_count = 5;
        for (int i = 0; i < compact_count; i++)
            graphics_draw_string(px + 9, py + 20 + i * 16,
                                 compact_controls[i], 0xFFE1DCEA);
        return;
    }
    graphics_draw_string(px + 9, py + 10, "FIELD KIT", 0xFFF2EDF5);
    graphics_fill_rect(px + 9, py + 30, panel_w - 18, 1, 0xFF746887);

    static const uint8_t materials[3] = {1, 3, 4};
    for (int i = 0; i < 3; i++) {
        int sy = py + 42 + i * 22;
        graphics_fill_rect(px + 9, sy, 14, 14, block_colors[materials[i]]);
        graphics_draw_rect(px + 9, sy, 14, 14,
                           i == selected_material ? 0xFFFFFFFF : 0xFF716988);
        graphics_draw_string(px + 30, sy, material_names[i],
                             i == selected_material ? 0xFFFFFFFF : 0xFFD0C9DC);
    }

    int controls_y = py + 116;
    graphics_fill_rect(px + 9, controls_y, panel_w - 18, 1, 0xFF746887);
    graphics_draw_string(px + 9, controls_y + 10, "W/S MOVE", 0xFFE1DCEA);
    graphics_draw_string(px + 9, controls_y + 28, "A/D STRAFE", 0xFFE1DCEA);
    graphics_draw_string(px + 9, controls_y + 46, "Q/E TURN", 0xFFE1DCEA);
    graphics_draw_string(px + 9, controls_y + 64, "C BLOCK", 0xFFE1DCEA);
    graphics_draw_string(px + 9, controls_y + 82, "SPACE ADD", 0xFFE1DCEA);
    graphics_draw_string(px + 9, controls_y + 100, "X MINE", 0xFFE1DCEA);
    graphics_draw_string(px + 9, controls_y + 118, "CLICK ADD", 0xFFE1DCEA);
    graphics_draw_string(px + 9, controls_y + 136, "R RESET", 0xFFE1DCEA);
}

void blocks_draw(int x, int y, int w, int h) {
    draw_world(x, y, w, h);
}

void blocks_handle_key(char key) {
    if (key == 'r' || key == 'R') {
        reset_world();
    } else if (key == 'c' || key == 'C') {
        selected_material = (selected_material + 1) % 3;
    } else if (key == ' ' ) {
        place_block();
    } else if (key == 'x' || key == 'X') {
        mine_block();
    } else if (key == 'q' || key == 'Q') {
        turn_player(-1);
    } else if (key == 'e' || key == 'E') {
        turn_player(1);
    } else if (key == 'w' || key == 'W') {
        walk_forward(1);
    } else if (key == 's' || key == 'S') {
        walk_forward(-1);
    } else if (key == 'a' || key == 'A') {
        strafe_player(-1);
    } else if (key == 'd' || key == 'D') {
        strafe_player(1);
    } else {
        return;
    }
}

void blocks_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if (mouse_x >= view_x && mouse_x < view_x + view_width &&
        mouse_y >= view_y && mouse_y < view_y + view_height)
        place_block();
}
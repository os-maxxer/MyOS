#include <solis/apps/blocks.h>
#include <solis/graphics.h>

#define WORLD_SIZE 32
#define WORLD_HEIGHT 24
#define SEA_LEVEL 5
#define FIXED_ONE 256
#define EYE_HEIGHT 192
#define RAY_MAX_STEPS 96
#define RAY_MAX_DISTANCE (26 * FIXED_ONE)
#define HOTBAR_SLOTS 9
#define TEX_SIZE 16
#define MAX_PARTICLES 56

enum {
    BLOCK_AIR = 0,
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_COBBLE,
    BLOCK_SAND,
    BLOCK_WOOD,
    BLOCK_PLANKS,
    BLOCK_LEAVES,
    BLOCK_COAL,
    BLOCK_IRON,
    BLOCK_WATER,
    BLOCK_BEDROCK,
    BLOCK_COUNT
};

static uint8_t world[WORLD_HEIGHT][WORLD_SIZE][WORLD_SIZE];
static int32_t player_x;
static int32_t player_y;
static int32_t camera_z;
static int player_cell_x;
static int player_cell_y;
static int selected_slot;
static int view_x;
static int view_y;
static int view_width;
static int view_height;
static int yaw;
static int jump_offset;
static int jump_velocity;
static int on_ground;
static int bob_phase;
static int bob_ticks;
static int action_ticks;
static int water_frame;
static int cloud_offset;
static uint32_t tick_counter;

struct particle {
    int x;
    int y;
    int vx;
    int vy;
    int life;
    int max_life;
    uint32_t color;
};

static struct particle particles[MAX_PARTICLES];

static const int direction_x[16] = {
    256, 237, 181, 98, 0, -98, -181, -237,
    -256, -237, -181, -98, 0, 98, 181, 237
};
static const int direction_y[16] = {
    0, 98, 181, 237, 256, 237, 181, 98,
    0, -98, -181, -237, -256, -237, -181, -98
};

static const uint32_t block_base[BLOCK_COUNT] = {
    0xFF000000, 0xFF6CBF4A, 0xFF8B5A2B, 0xFF8A8A8A,
    0xFF6F6F6F, 0xFFE4D6A0, 0xFF6B4A2A, 0xFFB07A45,
    0xFF3F8A3F, 0xFF3A3A3A, 0xFFC8A27A, 0xFF3B6EB5,
    0xFF252525
};

static const uint8_t hotbar_blocks[HOTBAR_SLOTS] = {
    BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_COBBLE, BLOCK_SAND,
    BLOCK_WOOD, BLOCK_PLANKS, BLOCK_LEAVES, BLOCK_COAL
};

static const uint8_t cloud_pattern[4][8] = {
    {0, 0, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 0}
};

static void spawn_block_particles(uint8_t id, int count);

struct ray_hit {
    int x;
    int y;
    int z;
    int previous_x;
    int previous_y;
    int previous_z;
    int face;
    int face_sign;
    int top;
    int bottom;
    int u;
    int v;
    int distance;
};

static int abs_i(int value) {
    return value < 0 ? -value : value;
}

static int clamp_byte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static uint32_t adjust_rgb(uint32_t color, int delta) {
    int r = clamp_byte((int)((color >> 16) & 0xFF) + delta);
    int g = clamp_byte((int)((color >> 8) & 0xFF) + delta);
    int b = clamp_byte((int)(color & 0xFF) + delta);
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t scale_color(uint32_t color, int numerator, int denominator) {
    if (denominator < 1) denominator = 1;
    int r = clamp_byte((int)((color >> 16) & 0xFF) * numerator / denominator);
    int g = clamp_byte((int)((color >> 8) & 0xFF) * numerator / denominator);
    int b = clamp_byte((int)(color & 0xFF) * numerator / denominator);
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t mix_color(uint32_t a, uint32_t b, int amount) {
    if (amount < 0) amount = 0;
    if (amount > 256) amount = 256;
    int ar = (int)((a >> 16) & 0xFF), ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int br = (int)((b >> 16) & 0xFF), bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int r = ar + (br - ar) * amount / 256;
    int g = ag + (bg - ag) * amount / 256;
    int bl = ab + (bb - ab) * amount / 256;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t tex_hash(uint32_t x, uint32_t y, uint32_t z, uint32_t salt) {
    uint32_t h = x * 374761393u + y * 668265263u +
                 z * 2246822519u + salt * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static int hash2(int x, int y, int seed) {
    return (int)(tex_hash((uint32_t)x, (uint32_t)y, (uint32_t)seed, 17u) & 0xFFFFu);
}

static int noise2(int x, int y, int scale, int seed) {
    int gx = x / scale;
    int gy = y / scale;
    int fx = x % scale;
    int fy = y % scale;
    int n00 = hash2(gx, gy, seed) & 255;
    int n10 = hash2(gx + 1, gy, seed) & 255;
    int n01 = hash2(gx, gy + 1, seed) & 255;
    int n11 = hash2(gx + 1, gy + 1, seed) & 255;
    int sx = fx * 256 / scale;
    int sy = fy * 256 / scale;
    int top = n00 + (n10 - n00) * sx / 256;
    int bottom = n01 + (n11 - n01) * sx / 256;
    return top + (bottom - top) * sy / 256;
}

static int terrain_height(int x, int y) {
    int broad = noise2(x, y, 14, 11);
    int detail = noise2(x, y, 5, 29);
    int h = 3 + broad * 5 / 255 + detail * 4 / 255;
    int dx = x - WORLD_SIZE / 2;
    int dy = y - WORLD_SIZE / 2;
    int spawn_distance = dx * dx + dy * dy;
    if (spawn_distance <= 9) h = 9;
    else if (spawn_distance <= 36 && h < 7) h = 7;
    if (h < 1) h = 1;
    if (h > WORLD_HEIGHT - 4) h = WORLD_HEIGHT - 4;
    return h;
}

static int is_solid(uint8_t block) {
    return block != BLOCK_AIR && block != BLOCK_WATER;
}

static int in_world(int x, int y, int z) {
    return x >= 0 && x < WORLD_SIZE && y >= 0 && y < WORLD_SIZE &&
           z >= 0 && z < WORLD_HEIGHT;
}

static int ground_level(int x, int y) {
    if (x < 0 || x >= WORLD_SIZE || y < 0 || y >= WORLD_SIZE) return 0;
    for (int z = WORLD_HEIGHT - 1; z >= 0; z--) {
        if (is_solid(world[z][y][x])) return z + 1;
    }
    return 0;
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

        if (!in_world(cell_x, cell_y, cell_z) || distance > RAY_MAX_DISTANCE)
            return 0;
        if (world[cell_z][cell_y][cell_x] == BLOCK_AIR) continue;

        int hit_x = player_x + ray_x * distance / FIXED_ONE;
        int hit_y = player_y + ray_y * distance / FIXED_ONE;
        int hit_z = camera_z + ray_z * distance / FIXED_ONE;

        hit->x = cell_x;
        hit->y = cell_y;
        hit->z = cell_z;
        hit->previous_x = previous_x;
        hit->previous_y = previous_y;
        hit->previous_z = previous_z;
        hit->face = face;
        hit->face_sign = face == 0 ? step_x : (face == 1 ? step_y : step_z);
        hit->top = face == 2 && step_z < 0;
        hit->bottom = face == 2 && step_z > 0;
        hit->distance = distance;

        if (face == 0) {
            hit->u = hit_y & 255;
            hit->v = hit_z & 255;
        } else if (face == 1) {
            hit->u = hit_x & 255;
            hit->v = hit_z & 255;
        } else {
            hit->u = hit_x & 255;
            hit->v = hit_y & 255;
        }
        return 1;
    }
    return 0;
}
static void update_camera(void) {
    int ground = ground_level(player_cell_x, player_cell_y);
    int bob = 0;
    if (bob_ticks > 0) {
        int phase = bob_phase & 7;
        int triangle = phase < 4 ? phase : 8 - phase;
        bob = triangle * 2;
    }
    camera_z = ground * FIXED_ONE + EYE_HEIGHT + jump_offset - bob;
    if (camera_z < EYE_HEIGHT) camera_z = EYE_HEIGHT;
}

static int spawn_view_score(int direction) {
    int score = 0;
    int center = WORLD_SIZE / 2;
    for (int distance = 4; distance <= 12; distance += 2) {
        int x = center + direction_x[direction] * distance / FIXED_ONE;
        int y = center + direction_y[direction] * distance / FIXED_ONE;
        if (x >= 0 && x < WORLD_SIZE && y >= 0 && y < WORLD_SIZE)
            score += ground_level(x, y);
    }
    return score;
}

static void reset_world(void) {
    for (int z = 0; z < WORLD_HEIGHT; z++)
        for (int y = 0; y < WORLD_SIZE; y++)
            for (int x = 0; x < WORLD_SIZE; x++)
                world[z][y][x] = BLOCK_AIR;

    for (int y = 0; y < WORLD_SIZE; y++) {
        for (int x = 0; x < WORLD_SIZE; x++) {
            int h = terrain_height(x, y);
            int beach = h <= SEA_LEVEL;
            world[0][y][x] = BLOCK_BEDROCK;
            for (int z = 1; z <= h; z++) {
                if (z == h) {
                    world[z][y][x] = beach ? BLOCK_SAND : BLOCK_GRASS;
                } else if (z >= h - 3) {
                    world[z][y][x] = BLOCK_DIRT;
                } else {
                    uint32_t ore = tex_hash((uint32_t)x * 13u + (uint32_t)z * 7u,
                                            (uint32_t)y * 17u + (uint32_t)z * 3u,
                                            41u, 7u);
                    if ((ore % 100u) < 2u)
                        world[z][y][x] = BLOCK_IRON;
                    else if ((ore % 100u) < 7u)
                        world[z][y][x] = BLOCK_COAL;
                    else
                        world[z][y][x] = BLOCK_STONE;
                }
            }
            for (int z = h + 1; z <= SEA_LEVEL; z++)
                world[z][y][x] = BLOCK_WATER;
        }
    }

    for (int ty = 3; ty < WORLD_SIZE - 3; ty++) {
        for (int tx = 3; tx < WORLD_SIZE - 3; tx++) {
            int h = ground_level(tx, ty) - 1;
            if (h <= SEA_LEVEL + 1 || h + 7 >= WORLD_HEIGHT) continue;
            if (world[h][ty][tx] != BLOCK_GRASS) continue;
            int dx = tx - WORLD_SIZE / 2;
            int dy = ty - WORLD_SIZE / 2;
            if (dx * dx + dy * dy <= 64) continue;
            uint32_t choice = tex_hash((uint32_t)tx, (uint32_t)ty, 71u, 23u);
            if ((choice % 100u) >= 5u) continue;

            int trunk = 3 + (int)((choice >> 8) & 1u);
            for (int tz = 1; tz <= trunk; tz++) {
                int wz = h + tz;
                if (wz < WORLD_HEIGHT) world[wz][ty][tx] = BLOCK_WOOD;
            }
            int base = h + trunk;
            for (int lz = -1; lz <= 2; lz++) {
                for (int ly = -2; ly <= 2; ly++) {
                    for (int lx = -2; lx <= 2; lx++) {
                        if (abs_i(lx) + abs_i(ly) + abs_i(lz) > 3) continue;
                        int wx = tx + lx;
                        int wy = ty + ly;
                        int wz = base + lz;
                        if (!in_world(wx, wy, wz)) continue;
                        if (world[wz][wy][wx] == BLOCK_AIR)
                            world[wz][wy][wx] = BLOCK_LEAVES;
                    }
                }
            }
        }
    }

    for (int y = WORLD_SIZE / 2 - 4; y <= WORLD_SIZE / 2 + 4; y++)
        for (int x = WORLD_SIZE / 2 - 4; x <= WORLD_SIZE / 2 + 4; x++)
            for (int z = 10; z < WORLD_HEIGHT - 4; z++)
                world[z][y][x] = BLOCK_AIR;

    player_x = (WORLD_SIZE / 2) * FIXED_ONE + FIXED_ONE / 2;
    player_y = (WORLD_SIZE / 2) * FIXED_ONE + FIXED_ONE / 2;
    player_cell_x = WORLD_SIZE / 2;
    player_cell_y = WORLD_SIZE / 2;
    int best_yaw = 0;
    int best_score = 0x7FFFFFFF;
    for (int i = 0; i < 16; i++) {
        int score = spawn_view_score(i);
        if (score < best_score) {
            best_score = score;
            best_yaw = i;
        }
    }
    yaw = best_yaw;
    selected_slot = 0;
    jump_offset = 0;
    jump_velocity = 0;
    on_ground = 1;
    bob_phase = 0;
    bob_ticks = 0;
    action_ticks = 0;
    water_frame = 0;
    cloud_offset = 0;
    tick_counter = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].life = 0;
    update_camera();
}

void blocks_init(void) {
    reset_world();
}

static void start_bob(void) {
    bob_phase = 0;
    bob_ticks = 6;
}

static int can_move_to(int x, int y) {
    if (x < 0 || x >= WORLD_SIZE || y < 0 || y >= WORLD_SIZE) return 0;
    int next_ground = ground_level(x, y);
    int current_ground = ground_level(player_cell_x, player_cell_y);
    if (next_ground <= 0 || next_ground >= WORLD_HEIGHT) return 0;
    if (abs_i(next_ground - current_ground) > 1) return 0;
    if (next_ground + 1 < WORLD_HEIGHT && world[next_ground + 1][y][x] != BLOCK_AIR)
        return 0;
    for (int z = next_ground; z < WORLD_HEIGHT; z++) {
        if (world[z][y][x] == BLOCK_WATER) return 0;
    }
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
    if (next_x == player_x && next_y == player_y) return;
    player_x = next_x;
    player_y = next_y;
    player_cell_x = cell_x;
    player_cell_y = cell_y;
    start_bob();
    update_camera();
}

static void walk_forward(int direction) {
    int dx = direction_x[yaw] * 96 / FIXED_ONE;
    int dy = direction_y[yaw] * 96 / FIXED_ONE;
    move_player(dx * direction, dy * direction);
}

static void strafe_player(int direction) {
    int dx = direction_y[yaw] * 96 / FIXED_ONE;
    int dy = -direction_x[yaw] * 96 / FIXED_ONE;
    move_player(dx * direction, dy * direction);
}

static void turn_player(int direction) {
    yaw = (yaw + direction + 16) % 16;
}

static void jump_player(void) {
    if (!on_ground) return;
    jump_velocity = 160;
    jump_offset = 0;
    on_ground = 0;
    action_ticks = 3;
    update_camera();
}

static void mine_block(void) {
    struct ray_hit hit;
    if (!cast_ray(direction_x[yaw], direction_y[yaw], 0, &hit)) return;
    uint8_t block = world[hit.z][hit.y][hit.x];
    if (block == BLOCK_AIR || block == BLOCK_WATER || block == BLOCK_BEDROCK)
        return;
    world[hit.z][hit.y][hit.x] = BLOCK_AIR;
    spawn_block_particles(block, 10);
    action_ticks = 4;
    update_camera();
}

static void place_block(void) {
    struct ray_hit hit;
    if (!cast_ray(direction_x[yaw], direction_y[yaw], 0, &hit)) return;
    int x = hit.previous_x;
    int y = hit.previous_y;
    int z = hit.previous_z;
    if (!in_world(x, y, z)) return;
    if (world[z][y][x] != BLOCK_AIR && world[z][y][x] != BLOCK_WATER) return;
    int ground = ground_level(player_cell_x, player_cell_y);
    if (x == player_cell_x && y == player_cell_y &&
        z >= ground - 1 && z <= ground + 2)
        return;
    uint8_t placed = hotbar_blocks[selected_slot];
    world[z][y][x] = placed;
    spawn_block_particles(placed, 6);
    action_ticks = 4;
    update_camera();
}
static uint32_t block_texel(uint8_t id, int face, int top, int bottom,
                            int u, int v, uint32_t salt) {
    uint32_t h = tex_hash((uint32_t)u, (uint32_t)v,
                          (uint32_t)id * 31u + (uint32_t)face * 7u, salt);
    int noise = (int)(h & 15u) - 7;
    uint32_t base = block_base[id];

    switch (id) {
        case BLOCK_GRASS:
            if (top) {
                if ((h & 255u) < 5u)
                    return 0xFFF2E27A;
                return adjust_rgb(0xFF6CBF4A, noise);
            }
            if (bottom) return adjust_rgb(0xFF8B5A2B, noise);
            if (v >= 13) return adjust_rgb(0xFF5EAE3D, noise);
            if (v == 12) return adjust_rgb(0xFF477F30, noise);
            return adjust_rgb(0xFF8B5A2B, noise / 2 + (int)((h >> 8) & 7u) - 3);
        case BLOCK_DIRT:
            if ((h & 7u) == 0u) return adjust_rgb(0xFF6B4320, noise);
            return adjust_rgb(0xFF8B5A2B, noise);
        case BLOCK_STONE:
            if (((u * 3 + v * 5 + (int)(h & 7u)) % 17) == 0)
                return adjust_rgb(0xFF5E5E5E, noise / 2);
            return adjust_rgb(0xFF8A8A8A, noise);
        case BLOCK_COBBLE:
            if ((u % 5) == 0 || ((v + u / 5) % 4) == 0)
                return adjust_rgb(0xFF555555, noise / 2);
            return adjust_rgb(0xFF808080, noise);
        case BLOCK_SAND:
            if ((h & 7u) == 0u) return adjust_rgb(0xFFC9B77A, noise);
            return adjust_rgb(0xFFE4D6A0, noise);
        case BLOCK_WOOD:
            if (top || bottom) {
                int d = abs_i(u - 7) + abs_i(v - 7);
                if (d > 10) return adjust_rgb(0xFF4A301A, noise / 2);
                if ((d % 3) == 0) return adjust_rgb(0xFF7A5730, noise / 2);
                return adjust_rgb(0xFFA77A48, noise);
            }
            if ((u % 4) == 0) return adjust_rgb(0xFF4C321A, noise / 2);
            if (((v + u / 4) % 7) == 0) return adjust_rgb(0xFF805C35, noise);
            return adjust_rgb(0xFF6B4A2A, noise);
        case BLOCK_PLANKS:
            if ((v % 4) == 0) return adjust_rgb(0xFF7E5128, noise / 2);
            if (((u + (v / 4) * 3) % 16) == 0) return adjust_rgb(0xFF8A5A2C, noise / 2);
            return adjust_rgb(0xFFB07A45, noise);
        case BLOCK_LEAVES:
            if ((h & 7u) < 2u) return adjust_rgb(0xFF245C25, noise / 2);
            if ((h & 7u) == 3u) return adjust_rgb(0xFF62B35A, noise);
            return adjust_rgb(0xFF3F8A3F, noise);
        case BLOCK_COAL:
            if ((h & 15u) < 3u) return adjust_rgb(0xFF242424, noise / 2);
            return adjust_rgb(0xFF8A8A8A, noise);
        case BLOCK_IRON:
            if ((h & 15u) < 3u) return adjust_rgb(0xFFC8A27A, noise / 2);
            return adjust_rgb(0xFF8A8A8A, noise);
        case BLOCK_WATER: {
            int wave = (u + v / 2 + water_frame * 3) % 8;
            if (wave < 2) return adjust_rgb(0xFF6A9AD0, noise / 2);
            if (wave == 2) return adjust_rgb(0xFF2F5C9E, noise / 2);
            return adjust_rgb(0xFF3B6EB5, noise / 2);
        }
        case BLOCK_BEDROCK:
            if (((u + v) & 1) == 0) return adjust_rgb(0xFF1A1A1A, noise / 2);
            return adjust_rgb(0xFF333333, noise / 2);
        default:
            return base;
    }
}

static int face_light(const struct ray_hit *hit) {
    if (hit->face == 2) return hit->top ? 256 : 150;
    if (hit->face == 0) return hit->face_sign > 0 ? 188 : 214;
    return hit->face_sign > 0 ? 174 : 202;
}

static uint32_t atmosphere_color(int row, int rows) {
    int horizon = rows * 55 / 100;
    if (horizon < 1) horizon = 1;
    if (row < horizon)
        return mix_color(0xFF3E74C8, 0xFFA9D0EA, row * 256 / horizon);
    if (rows <= horizon) return 0xFFA9D0EA;
    return mix_color(0xFFA9D0EA, 0xFFDDE9F2,
                     (row - horizon) * 256 / (rows - horizon));
}

static uint32_t sky_cell(int col, int row, int cols, int rows) {
    int horizon = rows * 55 / 100;
    if (horizon < 1) horizon = 1;
    uint32_t color = atmosphere_color(row, rows);
    if (row < horizon * 3 / 5) {
        int cu = ((col + yaw * 4 + cloud_offset * 2) / 12) & 7;
        int cv = (row / 8) & 3;
        if (cloud_pattern[cv][cu])
            color = (row & 8) ? 0xFFF6F9FC : 0xFFE4EDF5;
    }
    int sun_col = cols / 2 + (yaw & 7) * 9 - 28;
    int sun_row = horizon / 3;
    if (col >= sun_col && col < sun_col + 8 &&
        row >= sun_row && row < sun_row + 8)
        return 0xFFFFF2B0;
    if (row < horizon &&
        abs_i(col - sun_col - 4) < 14 && abs_i(row - sun_row - 4) < 10)
        color = mix_color(color, 0xFFFFE9A0, 96);
    return color;
}

static uint32_t block_color(const struct ray_hit *hit, int row, int rows) {
    uint8_t id = world[hit->z][hit->y][hit->x];
    uint32_t salt = (uint32_t)(hit->x * 13 + hit->y * 17 + hit->z * 19);
    uint32_t color = block_texel(id, hit->face, hit->top, hit->bottom,
                                 hit->u >> 4, hit->v >> 4, salt);
    int light = face_light(hit);
    if (hit->z + 1 < WORLD_HEIGHT &&
        is_solid(world[hit->z + 1][hit->y][hit->x]))
        light = light * 3 / 4;
    color = scale_color(color, light, 256);

    int fog = hit->distance * 160 / (FIXED_ONE * 24);
    if (fog > 180) fog = 180;
    return mix_color(color, atmosphere_color(row, rows), fog);
}
static void draw_block_icon(int x, int y, int size, uint8_t id) {
    if (size < 8) size = 8;
    int pixel = size / TEX_SIZE;
    if (pixel < 1) pixel = 1;
    int drawn = pixel * TEX_SIZE;
    int ox = x + (size - drawn) / 2;
    int oy = y + (size - drawn) / 2;
    uint32_t salt = (uint32_t)id * 53u;
    for (int v = 0; v < TEX_SIZE; v++) {
        for (int u = 0; u < TEX_SIZE; u++) {
            uint32_t color = block_texel(id, 0, 0, 0, u, v, salt);
            graphics_fill_rect(ox + u * pixel, oy + v * pixel,
                               pixel, pixel, color);
        }
    }
    graphics_draw_rect(x, y, size, size, 0xFF161616);
}

static int hud_slot_size(void) {
    int slot = view_width * 48 / 1024;
    if (slot < 22) slot = 22;
    if (slot > 72) slot = 72;
    return slot;
}

static uint32_t particle_color(uint8_t id, int index) {
    uint32_t color = block_texel(id, 0, 0, 0,
                                 (uint32_t)((index * 5 + 3) & 15),
                                 (uint32_t)((index * 3 + 7) & 15),
                                 (uint32_t)id * 53u);
    return adjust_rgb(color, (index & 1) ? 18 : -12);
}

static void spawn_block_particles(uint8_t id, int count) {
    if (view_width <= 0 || view_height <= 0) return;
    int cx = view_x + view_width / 2;
    int cy = view_y + view_height / 2;
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (particles[i].life > 0) continue;
        int angle = (spawned * 8 + (int)(tick_counter & 7)) & 15;
        particles[i].x = cx + ((spawned * 7) % 9) - 4;
        particles[i].y = cy + ((spawned * 5) % 9) - 4;
        particles[i].vx = direction_x[angle] * (2 + (spawned % 3)) / 128;
        particles[i].vy = -3 - (spawned % 4);
        particles[i].life = 12 + (spawned % 6);
        particles[i].max_life = particles[i].life;
        particles[i].color = particle_color(id, spawned);
        spawned++;
    }
}

static void draw_particles(void) {
    int size = view_width * 3 / 1024;
    if (size < 2) size = 2;
    if (size > 5) size = 5;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) continue;
        int pixel = size * particles[i].life / particles[i].max_life;
        if (pixel < 1) pixel = 1;
        graphics_fill_rect(particles[i].x, particles[i].y,
                           pixel, pixel, particles[i].color);
    }
}

static void draw_heart(int x, int y, int pixel) {
    static const uint8_t pattern[6][7] = {
        {0, 1, 1, 0, 1, 1, 0},
        {1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 1, 1, 0},
        {0, 0, 1, 1, 1, 0, 0},
        {0, 0, 0, 1, 0, 0, 0}
    };
    graphics_fill_rect(x - pixel, y - pixel, 9 * pixel, 8 * pixel, 0xFF341010);
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            if (pattern[row][col])
                graphics_fill_rect(x + col * pixel, y + row * pixel,
                                   pixel, pixel, 0xFFD92B2B);
        }
    }
    if (pixel > 1)
        graphics_fill_rect(x + pixel, y + pixel, pixel, pixel, 0xFFFF6B6B);
}

static void draw_hunger(int x, int y, int pixel) {
    static const uint8_t pattern[6][7] = {
        {0, 0, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 0, 0},
        {0, 0, 1, 1, 0, 0, 0}
    };
    graphics_fill_rect(x - pixel, y - pixel, 9 * pixel, 8 * pixel, 0xFF2A1A0C);
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            if (pattern[row][col])
                graphics_fill_rect(x + col * pixel, y + row * pixel,
                                   pixel, pixel, 0xFFB56A2A);
        }
    }
    graphics_fill_rect(x + 5 * pixel, y + 4 * pixel, 2 * pixel, 2 * pixel,
                       0xFFE8D8B0);
}

static void draw_hotbar(void) {
    int slot = hud_slot_size();
    int gap = slot / 20;
    if (gap < 2) gap = 2;
    if (gap > 4) gap = 4;
    int total = HOTBAR_SLOTS * slot + (HOTBAR_SLOTS - 1) * gap;
    int hx = view_x + (view_width - total) / 2;
    int hy = view_y + view_height - slot - 14;

    graphics_fill_rect(hx - 4, hy - 4, total + 8, slot + 8, 0xFF101010);
    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        int sx = hx + i * (slot + gap);
        graphics_fill_rect(sx, hy, slot, slot, 0xFF383838);
        graphics_draw_rect(sx, hy, slot, slot, 0xFF9A9A9A);
        if (i == selected_slot) {
            graphics_draw_rect(sx - 2, hy - 2, slot + 4, slot + 4, 0xFF000000);
            graphics_draw_rect(sx - 1, hy - 1, slot + 2, slot + 2, 0xFFF4F4F4);
        }
        draw_block_icon(sx + 3, hy + 3, slot - 6, hotbar_blocks[i]);
    }

    int icon = slot >= 44 ? 3 : (slot >= 30 ? 2 : 1);
    int step = 7 * icon + 1;
    int bars_w = 10 * step - 1;
    int bar_y = hy - 6 * icon - 8;
    int hearts_x = hx + 2;
    int hunger_x = hx + total - bars_w - 2;
    for (int i = 0; i < 10; i++) {
        draw_heart(hearts_x + i * step, bar_y, icon);
        draw_hunger(hunger_x + i * step, bar_y, icon);
    }
}

static void draw_hand(void) {
    if (view_width < 400 || view_height < 240) return;
    int bob = 0;
    if (bob_ticks > 0) {
        int phase = bob_phase & 7;
        int triangle = phase < 4 ? phase : 8 - phase;
        bob = triangle * 2;
    }
    if (action_ticks > 0) bob += 5;

    int size = view_height * 50 / 768;
    if (size < 42) size = 42;
    if (size > 96) size = 96;
    int margin = view_width * 188 / 1024;
    if (margin < 120) margin = 120;
    if (margin > 300) margin = 300;
    int base_x = view_x + view_width - margin + bob / 2;
    int base_y = view_y + view_height - 18 + bob;

    int hx = base_x - 58;
    int hy = base_y - size - 178;
    draw_block_icon(hx, hy, size, hotbar_blocks[selected_slot]);
    uint32_t base = block_base[hotbar_blocks[selected_slot]];
    graphics_fill_rect(hx, hy, size, 4, scale_color(base, 320, 256));
    graphics_fill_rect(hx + size - 4, hy, 4, size, scale_color(base, 160, 256));
    graphics_draw_rect(hx - 1, hy - 1, size + 2, size + 2, 0xFF101010);

    graphics_fill_rect(base_x + 58, base_y - 8, 150, 38, 0xFF2F6F9E);
    graphics_fill_rect(base_x + 46, base_y - 44, 140, 42, 0xFF2F6F9E);
    graphics_fill_rect(base_x + 34, base_y - 78, 128, 40, 0xFF2F6F9E);
    graphics_fill_rect(base_x + 22, base_y - 110, 112, 38, 0xFF2F6F9E);
    graphics_fill_rect(base_x + 66, base_y - 2, 136, 5, 0xFF5FA5D6);
    graphics_fill_rect(base_x + 50, base_y - 34, 126, 5, 0xFF5FA5D6);

    graphics_fill_rect(base_x + 2, base_y - 150, 44, 52, 0xFFE0B08A);
    graphics_fill_rect(base_x - 8, base_y - 184, 36, 42, 0xFFE0B08A);
    graphics_fill_rect(base_x - 12, base_y - 202, 10, 24, 0xFFE0B08A);
    graphics_fill_rect(base_x + 2, base_y - 205, 10, 26, 0xFFE0B08A);
    graphics_fill_rect(base_x + 16, base_y - 202, 10, 22, 0xFFE0B08A);
    graphics_fill_rect(base_x + 38, base_y - 150, 6, 40, 0xFFC98E6E);
    graphics_fill_rect(base_x - 9, base_y - 182, 6, 12, 0xFFC98E6E);
}

static void draw_compass(void) {
    static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = ((yaw & 15) * 8) / 16;
    if (idx < 0) idx += 8;
    if (idx >= 8) idx -= 8;
    char label[40];
    int n = 0;
    const char *prefix = "Facing ";
    while (*prefix) label[n++] = *prefix++;
    const char *dir = dirs[idx];
    while (*dir) label[n++] = *dir++;
    label[n++] = ' '; label[n++] = ' '; label[n++] = 'X';
    char num[8]; int ni = 0; int value = player_cell_x;
    if (value < 0) { label[n++] = '-'; value = -value; }
    if (value == 0) num[ni++] = '0';
    while (value > 0) { num[ni++] = '0' + value % 10; value /= 10; }
    while (ni > 0) label[n++] = num[--ni];
    label[n++] = ' '; label[n++] = 'Y';
    ni = 0; value = player_cell_y;
    if (value < 0) { label[n++] = '-'; value = -value; }
    if (value == 0) num[ni++] = '0';
    while (value > 0) { num[ni++] = '0' + value % 10; value /= 10; }
    while (ni > 0) label[n++] = num[--ni];
    label[n] = '\0';
    graphics_fill_rect(view_x + 12, view_y + 12, 188, 24, 0x99000000);
    graphics_draw_string(view_x + 20, view_y + 17, label, 0xFFFFFFFF);
}

static void draw_crosshair(void) {
    int cx = view_x + view_width / 2;
    int cy = view_y + view_height / 2;
    int arm = view_width * 10 / 1024;
    if (arm < 8) arm = 8;
    if (arm > 16) arm = 16;
    int thick = arm / 3;
    if (thick < 3) thick = 3;
    int inner = arm - 2;
    if (inner < 4) inner = 4;
    uint32_t color = action_ticks > 0 ? 0xFFFFD060 : 0xFFFFFFFF;
    graphics_fill_rect(cx - arm, cy - thick / 2 - 1,
                       arm * 2 + 1, thick + 2, 0xFF1A1A1A);
    graphics_fill_rect(cx - arm + 2, cy - thick / 2,
                       inner * 2 - 1, thick, color);
    graphics_fill_rect(cx - thick / 2 - 1, cy - arm,
                       thick + 2, arm * 2 + 1, 0xFF1A1A1A);
    graphics_fill_rect(cx - thick / 2, cy - arm + 2,
                       thick, inner * 2 - 1, color);
}
static void draw_world(int x, int y, int w, int h) {
    view_x = x;
    view_y = y;
    view_width = w;
    view_height = h;

    int scale = 2;
    if (w >= 1200) scale = 4;
    else if (w >= 700) scale = 3;
    else if (w < 480 || h < 300) scale = 1;
    int ray_width = view_width / scale;
    int ray_height = view_height / scale;
    if (ray_width < 1) ray_width = 1;
    if (ray_height < 1) ray_height = 1;
    int center_row = ray_height * 46 / 100;
    int dir_x = direction_x[yaw];
    int dir_y = direction_y[yaw];
    int plane_x = -dir_y * 160 / FIXED_ONE;
    int plane_y = dir_x * 160 / FIXED_ONE;

    for (int row = 0; row < ray_height; row++) {
        int ray_z = (center_row - row) * 256 / ray_height;
        for (int col = 0; col < ray_width; col++) {
            int side = ((2 * col + 1 - ray_width) * 160) / ray_width;
            int ray_x = dir_x + plane_x * side / FIXED_ONE;
            int ray_y = dir_y + plane_y * side / FIXED_ONE;
            struct ray_hit hit;
            uint32_t color = sky_cell(col, row, ray_width, ray_height);
            if (cast_ray(ray_x, ray_y, ray_z, &hit))
                color = block_color(&hit, row, ray_height);

            int px0 = view_x + col * view_width / ray_width;
            int px1 = view_x + (col + 1) * view_width / ray_width;
            int py0 = view_y + row * view_height / ray_height;
            int py1 = view_y + (row + 1) * view_height / ray_height;
            graphics_fill_rect(px0, py0, px1 - px0, py1 - py0, color);
        }
    }

    draw_crosshair();
    draw_compass();
    draw_hotbar();
    draw_hand();
    draw_particles();
}

void blocks_draw(int x, int y, int w, int h) {
    draw_world(x, y, w, h);
}

int blocks_tick(void) {
    int dirty = 0;
    tick_counter++;

    if (jump_offset > 0 || jump_velocity != 0) {
        jump_velocity -= 40;
        jump_offset += jump_velocity;
        if (jump_offset <= 0) {
            jump_offset = 0;
            jump_velocity = 0;
            on_ground = 1;
        }
        dirty = 1;
    }
    if (bob_ticks > 0) {
        bob_phase = (bob_phase + 1) & 7;
        bob_ticks--;
        dirty = 1;
    }
    if (action_ticks > 0) {
        action_ticks--;
        dirty = 1;
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) continue;
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vy++;
        particles[i].life--;
        if (particles[i].x < view_x ||
            particles[i].x >= view_x + view_width ||
            particles[i].y < view_y ||
            particles[i].y >= view_y + view_height)
            particles[i].life = 0;
        dirty = 1;
    }
    if ((tick_counter % 16u) == 0u) {
        water_frame = (water_frame + 1) & 3;
        dirty = 1;
    }
    if ((tick_counter % 64u) == 0u) {
        cloud_offset = (cloud_offset + 1) & 63;
        dirty = 1;
    }
    if (dirty) update_camera();
    return dirty;
}

void blocks_handle_key(char key) {
    if (key == 'r' || key == 'R') {
        reset_world();
    } else if (key >= '1' && key <= '9') {
        selected_slot = key - '1';
    } else if (key == 'c' || key == 'C') {
        selected_slot = (selected_slot + 1) % HOTBAR_SLOTS;
    } else if (key == 'f' || key == 'F') {
        place_block();
    } else if (key == ' ') {
        jump_player();
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
    }
}

void blocks_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if (mouse_x >= view_x && mouse_x < view_x + view_width &&
        mouse_y >= view_y && mouse_y < view_y + view_height)
        mine_block();
}

void blocks_handle_context(int x, int y, int w, int h, int mouse_x, int mouse_y) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if (mouse_x >= view_x && mouse_x < view_x + view_width &&
        mouse_y >= view_y && mouse_y < view_y + view_height)
        place_block();
}

void blocks_get_size(int *w, int *h) {
    if (w) *w = 760;
    if (h) *h = 560;
}

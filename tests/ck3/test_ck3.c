/* Host-side tests for the logic in apps/ck3.c that does not need a VM.
 *
 * The app is compiled for a freestanding kernel with no libc, but most of what
 * can silently ruin a campaign -- the province hit-test, the march path, the
 * casus-belli rules, the save format -- is plain integer code. This file
 * supplies stub implementations of the platform surface, includes the app
 * itself (so every static helper is reachable), and asserts on the results.
 *
 * Build and run with:  make test-ck3
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <solis/graphics.h>
#include <solis/timer.h>
#include <solis/vfs.h>
#include <solis/ports.h>
#include <solis/rtc.h>
#include <solis/syscall.h>

/* ---- platform stubs ------------------------------------------------- */

static int stub_ticks;
static uint32_t stub_width = 1000, stub_height = 680;

void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void graphics_draw_string(uint32_t x, uint32_t y, const char *text,
                          uint32_t color) {
    (void)x; (void)y; (void)text; (void)color;
}
uint32_t graphics_get_width(void) { return stub_width; }
uint32_t graphics_get_height(void) { return stub_height; }
uint32_t timer_get_ticks(void) { return (uint32_t)stub_ticks; }
void outb(uint16_t port, uint8_t value) { (void)port; (void)value; }

void rtc_get_time(struct rtc_time *out) {
    if (!out) return;
    out->second = 30;
    out->minute = 15;
    out->hour = 9;
    out->day = 21;
    out->month = 11;
    out->year = 2026;
    out->weekday = 6;
    out->tz_offset = 0;
    out->tz_index = 0;
}

const char *sys_get_build_id(void) { return "test-build"; }

/* A one-file in-memory filesystem: enough to exercise save/load exactly. */
static uint8_t fs_buf[32768];
static int fs_size;
static int fs_pos;

int vfs_create(const char *path) {
    (void)path;
    fs_pos = 0;
    fs_size = 0;
    return 1;
}
int vfs_open(const char *path) {
    (void)path;
    if (fs_size <= 0) return -1;
    fs_pos = 0;
    return 1;
}
int vfs_read(int fd, uint8_t *buf, uint32_t size) {
    (void)fd;
    int avail = fs_size - fs_pos;
    if ((int)size > avail) size = (uint32_t)(avail > 0 ? avail : 0);
    memcpy(buf, fs_buf + fs_pos, size);
    fs_pos += (int)size;
    return (int)size;
}
int vfs_write(int fd, const uint8_t *buf, uint32_t size) {
    (void)fd;
    if (fs_pos + (int)size > (int)sizeof(fs_buf)) return -1;
    memcpy(fs_buf + fs_pos, buf, size);
    fs_pos += (int)size;
    if (fs_pos > fs_size) fs_size = fs_pos;
    return (int)size;
}
int vfs_get_size(int fd) { (void)fd; return fs_size; }

/* The app under test. */
#include "../../apps/ck3.c"

/* ---- tiny test harness ---------------------------------------------- */

static int failures;
static int checks;

static void check(int condition, const char *what) {
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL %s\n", what);
    }
}

static void test_hit_test(void) {
    printf("province hit test\n");
    /* At native map size the pixel -> province mapping must be exact; this is
     * the path a left click takes, and an off-by-one picks the wrong county. */
    map_x0 = 0;
    map_y0 = 0;
    map_w0 = CK3_MAP_W;
    map_h0 = CK3_MAP_H;

    int sampled = 0, wrong = 0;
    for (int row = 0; row < CK3_MAP_H; row += 17) {
        for (int col = 0; col < GRID_COLS; col += 11) {
            int want = grid[row][col];
            if (want == SEAP) continue;
            int px = col * SAMPLE + SAMPLE / 2;
            int got = province_at(px, row);
            sampled++;
            if (got != want) wrong++;
        }
    }
    check(sampled > 500, "sampled enough land cells");
    check(wrong == 0, "every sampled land cell maps back to its province");

    /* Outside the map there is no province, not province 0. */
    check(province_at(-5, 10) == -1, "clicks left of the map select nothing");
    check(province_at(10, CK3_MAP_H + 40) == -1,
          "clicks below the map select nothing");
}

static void test_pathfinding(void) {
    printf("march pathfinding\n");
    int samples = 0;
    for (int from = 0; from < PROV_MAX && samples < 6; from++) {
        int to = (from + 37) % PROV_MAX;
        int hop = path_next(from, to);
        if (hop == 255) continue;
        samples++;
        /* The first step must be a real neighbour of the starting province. */
        int adjacent = 0;
        for (int k = 0; k < adj_n[from]; k++)
            if (adj[from][k] == hop) adjacent = 1;
        check(adjacent, "the first march step is an adjacent province");
        check(path_next(from, from) == 255, "no step when already there");

        /* Walk the path and require it to terminate at the destination. */
        int cur = from, steps = 0, reached = 0;
        while (steps < 200) {
            int n = path_next(cur, to);
            if (n == 255) break;
            cur = n;
            steps++;
            if (cur == to) { reached = 1; break; }
        }
        check(reached, "the path reaches the destination");
    }
    check(samples > 0, "found provinces to path between");
}

static void test_casus_belli(void) {
    printf("casus belli rules\n");
    player = 0;
    year = 1100;
    month = 6;
    realm[player].war = 0;

    int foreign = -1;
    for (int p = 0; p < PROV_MAX && foreign < 0; p++)
        if (prov[p].owner != player) foreign = p;
    check(foreign >= 0, "found a foreign province");
    if (foreign < 0) return;

    int enemy = prov[foreign].owner;
    prov[foreign].claim = 0;
    realm[player].prestige = 60;
    truce[player][enemy] = 0;
    check(casus_available(CB_CONQUEST, foreign) == 1,
          "conquest is offered with 50 prestige");
    check(casus_available(CB_CLAIM, foreign) == 0,
          "claim is refused without a fabricated claim");

    prov[foreign].claim = 1;
    realm[player].prestige = 25;
    check(casus_available(CB_CLAIM, foreign) == 1,
          "a sealed claim is enough at 25 prestige");

    /* A truce blocks every cause until it expires. */
    truce[player][enemy] = (uint16_t)(year * 12 + month + 12);
    check(casus_available(CB_CONQUEST, foreign) == 0,
          "a standing truce blocks a new war");
    truce[player][enemy] = (uint16_t)(year * 12 + month - 1);
    realm[player].prestige = 60;
    check(casus_available(CB_CONQUEST, foreign) == 1,
          "an expired truce allows war again");

    check(casus_available(CB_CONQUEST, ck3_realm_defs[player].capital) == 0,
          "no war against your own province");
}

static void test_battle_is_seeded(void) {
    printf("battle resolution\n");
    int a = new_army(0, ck3_realm_defs[0].capital, 600);
    int b = new_army(1, ck3_realm_defs[0].capital, 500);
    check(a >= 0 && b >= 0, "raised two armies");
    if (a < 0 || b < 0) return;

    rng_state = 12345;
    int before_a = armies[a].men, before_b = armies[b].men;
    resolve_battle(a, b);
    int lost_a = before_a - armies[a].men;
    int lost_b = before_b - armies[b].men;

    /* With the same seed and the same state the roll must repeat. */
    armies[a].men = (uint16_t)before_a;
    armies[b].men = (uint16_t)before_b;
    armies[a].alive = 1;
    armies[b].alive = 1;
    armies[a].morale = 100;
    armies[b].morale = 100;
    rng_state = 12345;
    resolve_battle(a, b);
    check(before_a - armies[a].men == lost_a && before_b - armies[b].men == lost_b,
          "the same seed reproduces the same losses");
    check(lost_a >= 0 && lost_b >= 0, "losses are never negative");

    armies[a].alive = 0;
    armies[b].alive = 0;
}

static void test_save_roundtrip(void) {
    printf("save and load\n");
    fs_size = 0;
    fs_pos = 0;
    player = 0;
    year = 1123;
    month = 9;
    prov[0].dev = 7;
    prov[0].owner = 3;
    realm[player].gold = 321;
    realm[player].opinion = 44;
    truce[0][1] = 13500;
    scopy(chron[0], "A test chronicle line.", 64);
    chron_count = 1;

    check(save_game() == 1, "save writes the campaign");
    check(save_present() == 1, "a valid save is detected");
    check(fs_size == (int)sizeof(struct save_blob), "the save has one record");

    /* Damage the state, then load and expect the file to win. */
    prov[0].dev = 1;
    prov[0].owner = 0;
    year = 1066;
    month = 1;
    realm[player].gold = 0;
    realm[player].opinion = 10;
    truce[0][1] = 0;
    chron_count = 0;

    check(load_game() == 1, "load accepts its own save");
    check(year == 1123 && month == 9, "the date is restored");
    check(prov[0].dev == 7 && prov[0].owner == 3, "province state is restored");
    check(realm[player].gold == 321 && realm[player].opinion == 44,
          "realm state is restored");
    check(truce[0][1] == 13500, "truces are restored");
    check(chron_count == 1 &&
          strcmp(chron[0], "A test chronicle line.") == 0,
          "the chronicle is restored");

    /* A corrupted byte must be rejected rather than loaded. */
    fs_buf[40] ^= 0xFF;
    check(load_game() == 0, "a corrupted save is refused");
}

int main(void) {
    printf("ck3 host tests\n");
    ck3_init();
    check(world_built == 1, "the world raster is built once");
    test_hit_test();
    test_pathfinding();
    test_casus_belli();
    test_battle_is_seeded();
    test_save_roundtrip();

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}

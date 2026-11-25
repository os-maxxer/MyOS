/* Crusader Kings III - a Solis chronicle.
 *
 * A compact feudal grand-strategy game in the spirit of the Paradox classic.
 * The map is the world of 1066: thirty-seven realms from Ireland to the
 * Great Liao, drawn from a real 1:50m coastline and divided into provinces
 * by nearest-seed (Voronoi) cells, so a province is a seed and its managed
 * land, never a hand-cut polygon.
 *
 * You pick any realm on the map, then rule it from a court bar at the bottom
 * of the screen: the ruler on the left, tabs for decisions, the realm, the
 * armies and the current war. Right-clicking a province opens its own menu
 * (declare war, fabricate a claim, raise levies, march an army there). Wars
 * are fought with real hosts that march province to province, besiege forts
 * and fight battles, and every realm runs the same systems through a simple
 * AI that declares wars, marches, sieges and sends you offers.
 *
 * The kernel is built with -mno-sse and -mno-80387, so there is no floating
 * point anywhere: the projection, the economy and the combat rolls are all
 * integer arithmetic.
 */

#include <solis/apps/ck3.h>
#include <solis/graphics.h>
#include <solis/timer.h>
#include <solis/vfs.h>
#include <solis/ports.h>
#include <solis/rtc.h>
#include <solis/buildinfo.h>
#include <solis/apps/ck3_map.h>

#define PROV_MAX   CK3_PROV_COUNT
#define REALM_MAX  CK3_REALM_COUNT
#define ARMY_MAX   48
#define WAR_MAX    12

/* Two years of enforced peace after any treaty. Without it the AI declares
 * war again the month after signing, which makes peace look broken. */
#define TRUCE_MONTHS 24
#define CK3_SAVE_PATH "home/ck3.save"
#define CK3_SAVE_MAGIC 0x43334B53u  /* "CK3S" */
#define CK3_SAVE_VERSION 1
#define MAX_DEV    9
#define MAX_FORT   3

#define SAMPLE     2
#define GRID_COLS  ((CK3_MAP_W + SAMPLE - 1) / SAMPLE)
#define SEAP     255

/* Terrain. */
#define T_PLAINS   0
#define T_FOREST   1
#define T_HILLS    2
#define T_MARSH    3
#define T_DESERT   4
#define T_MOUNTAIN 5

/* Faiths. */
#define F_CHRISTIAN 0
#define F_SUNNI     1
#define F_SHIA      2
#define F_PAGAN     3
#define F_HINDU     4
#define F_BUDDHIST  5

/* Screens. */
#define SCREEN_TITLE 0
#define SCREEN_PICK  1
#define SCREEN_GAME  2
#define SCREEN_HELP  3

/* Side panels. */
#define PANEL_NONE      0
#define PANEL_DECISIONS 1
#define PANEL_REALM     2
#define PANEL_ARMY      3
#define PANEL_WAR       4
#define PANEL_CHRONICLE 5

/* Modal windows. */
#define MODAL_NONE      0
#define MODAL_EVENT     1
#define MODAL_WAR_DECL  2
#define MODAL_PEACE     3

/* Casus belli. */
#define CB_CLAIM     0
#define CB_HOLY      1
#define CB_CONQUEST  2
#define CB_RAID      3
#define CB_COUNT     4

/* Palette: the parchment map. */
#define C_SEA       0xFFB6B09C
#define C_SEA_DEEP  0xFFADA792
#define C_LAND      0xFFE3D7B9
#define C_COAST     0xFF585947
#define C_INK       0xFF38352B
#define C_PAPER     0xFFDED2AE
#define C_SEA_TEXT  0xFF74725E
#define C_REALM_LINE 0xFF6C6350

/* Palette: the court UI. */
#define C_UI        0xFF14161D
#define C_UI2       0xFF1D212C
#define C_UI3       0xFF262B38
#define C_LINE      0xFF333A4A
#define C_TEXT      0xFFE9E4D6
#define C_DIM       0xFF97A0AF
#define C_GOLD      0xFFE6C36B
#define C_RED       0xFFC4574A
#define C_GREEN     0xFF6FAF7A
#define C_SEL       0xFFF3D98F
#define C_PIETY     0xFFA18CE0
#define C_MAP_SEL   0xFFF3D98F

struct province_state {
    uint8_t owner;
    uint8_t terrain;
    uint8_t dev;
    uint8_t fort;
    uint8_t occ;       /* occupying realm + 1, 0 = free */
    uint8_t claim;     /* 1 when the player has a claim */
    uint16_t levy;     /* men that can still be raised here */
    int16_t seat_x;    /* map pixel of the province's centre */
    int16_t seat_y;
};

struct realm_state {
    int16_t gold;
    int16_t prestige;
    int16_t piety;
    uint8_t alive;
    uint8_t age;
    uint8_t health;
    uint8_t heir;
    uint8_t heir_age;
    uint8_t war;        /* active war index + 1, 0 = at peace */
    uint8_t offered;    /* AI has already offered peace in this war */
    int8_t  opinion;    /* how the great vassals regard this ruler, 0..100 */
};

struct army_state {
    uint8_t alive;
    uint8_t owner;
    uint8_t prov;
    uint8_t target;     /* province index, 255 = hold */
    uint8_t next;       /* next hop, 255 = no path */
    uint8_t delay;      /* months still to wait (sea crossings) */
    uint8_t morale;
    uint8_t siege;
    uint16_t men;
};

struct war_state {
    uint8_t active;
    uint8_t attacker;
    uint8_t defender;
    uint8_t target;
    uint8_t casus;
    uint8_t months;
    int16_t score;      /* positive favours the attacker */
};

static struct province_state prov[PROV_MAX];
static struct realm_state realm[REALM_MAX];
static struct army_state armies[ARMY_MAX];
static struct war_state wars[WAR_MAX];

/* The political raster: every land sample belongs to a province. */
static uint8_t grid[CK3_MAP_H][GRID_COLS];
static uint8_t adj[PROV_MAX][12];
static uint8_t adj_n[PROV_MAX];
static uint8_t adj_sea[PROV_MAX];     /* bit i: adj[i] is a sea crossing */
static int16_t seed_px[PROV_MAX];
static int16_t seed_py[PROV_MAX];
static uint32_t prov_fill[PROV_MAX];
static uint32_t prov_edge[PROV_MAX];
static uint32_t prov_sel[PROV_MAX];

static int world_built;
static int started;
static int screen = SCREEN_TITLE;
static int player;
static int selected_prov;
static int selected_army = -1;
static int panel = PANEL_NONE;
static int modal = MODAL_NONE;
static int modal_arg;
static int event_kind;
static int year, month;
static int speed;                 /* 0 paused, 1..3 */
static uint32_t last_advance;
static int game_over;
static int pick_sel;
static int pick_scroll;
static int menu_sel;
static int ctx_open;
static int ctx_x, ctx_y;
static int ctx_count;
static char ctx_label[6][48];
static int ctx_prov = -1;
static int map_mode;              /* 0 political, 1 development */
static int wdecl_prov = -1;
static int ctx_action[6];
static int ctx_enabled[6];
static uint32_t rng_state;

static int casus_available(int cb, int p);
static int peace_available(int opt);
static const char *casus_name(int cb);
static int realm_provinces(int r);
static int realm_levies(int r);
static int realm_armies(int r);
static int realm_host(int r);

static char chron[10][64];
static int chron_count;

/* Truce table in absolute months (year * 12 + month) per realm pair. */
static uint16_t truce[REALM_MAX][REALM_MAX];
/* The last headline about a war between two other realms, shown in the
 * header so foreign politics are legible without opening a panel. */
static char ticker[64];
static int debug_overlay;
static uint32_t campaign_seed;
static int save_ok;
static int repaint_ms;

/* Map transform, set by draw_map and reused by projection and markers. */
static int map_x0, map_y0, map_w0, map_h0;

/* ------------------------------------------------------------------ */
/* Small helpers (apps have no libc)                                   */
/* ------------------------------------------------------------------ */

static int slen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void scopy(char *dst, const char *src, int max) {
    int i = 0;
    if (max <= 0) return;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void sappend(char *dst, const char *src, int max) {
    int i = slen(dst);
    int j = 0;
    if (max <= 0) return;
    while (src && src[j] && i < max - 1) dst[i++] = src[j++];
    dst[i] = '\0';
}

static void snum(char *dst, int value, int max) {
    char tmp[12];
    int n = 0, neg = 0;
    int i = slen(dst);
    if (value < 0) { neg = 1; value = -value; }
    if (value == 0) tmp[n++] = '0';
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    if (neg && i < max - 1) dst[i++] = '-';
    while (n > 0 && i < max - 1) dst[i++] = tmp[--n];
    dst[i] = '\0';
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 16;
}

static int rnd_below(int n) {
    if (n <= 0) return 0;
    return (int)(rnd() % (uint32_t)n);
}

static uint32_t mix_color(uint32_t a, uint32_t b, int t) {
    int ar = (int)((a >> 16) & 255), ag = (int)((a >> 8) & 255), ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255), bg = (int)((b >> 8) & 255), bb = (int)(b & 255);
    int r = (ar * (256 - t) + br * t) >> 8;
    int g = (ag * (256 - t) + bg * t) >> 8;
    int c = (ab * (256 - t) + bb * t) >> 8;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)c;
}

static uint32_t shade_color(uint32_t c, int percent) {
    uint32_t r = ((c >> 16) & 255) * (uint32_t)percent / 100u;
    uint32_t g = ((c >> 8) & 255) * (uint32_t)percent / 100u;
    uint32_t b = (c & 255) * (uint32_t)percent / 100u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void chron_add(const char *text) {
    if (chron_count < 10) {
        scopy(chron[chron_count], text, 64);
        chron_count++;
    } else {
        for (int i = 1; i < 10; i++) scopy(chron[i - 1], chron[i], 64);
        scopy(chron[9], text, 64);
    }
}

static int abs_month(void) { return year * 12 + month; }

/* COM1, set up by the kernel's debug console. Writing here is what lets a
 * headless QEMU run report what the simulation actually did, instead of
 * needing a mouse and a screenshot. */
static void serial_out(const char *s) {
    for (; s && *s; s++) {
        if (*s == '\n') outb(0x3F8, (uint8_t)'\r');
        outb(0x3F8, (uint8_t)*s);
    }
}

static uint32_t crc32_of(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */

/* One flat record, written to SolFS as-is. The version and CRC make a
 * truncated or half-written save a clean "no save" instead of a corrupt
 * campaign. Bump CK3_SAVE_VERSION whenever this struct changes. */
struct save_blob {
    uint32_t magic;
    uint16_t version;
    uint16_t saved_year;
    uint8_t  saved_month;
    uint8_t  saved_player;
    uint8_t  saved_map_mode;
    uint8_t  saved_pad;
    uint32_t rng;
    uint32_t seed;
    uint8_t  saved_chron_count;
    char     saved_chron[10][64];
    struct province_state prov[PROV_MAX];
    struct realm_state realm[REALM_MAX];
    struct army_state armies[ARMY_MAX];
    struct war_state wars[WAR_MAX];
    uint16_t truce[REALM_MAX][REALM_MAX];
    uint32_t crc;
};

static struct save_blob save_buf;

static uint32_t save_crc_offset(void) {
    return (uint32_t)((const uint8_t *)&save_buf.crc - (const uint8_t *)&save_buf);
}

static int save_present(void) {
    int fd = vfs_open(CK3_SAVE_PATH);
    if (fd < 0) return 0;
    return vfs_get_size(fd) == (int)sizeof(struct save_blob);
}

static int save_game(void) {
    save_buf.magic = CK3_SAVE_MAGIC;
    save_buf.version = CK3_SAVE_VERSION;
    save_buf.saved_year = (uint16_t)year;
    save_buf.saved_month = (uint8_t)month;
    save_buf.saved_player = (uint8_t)player;
    save_buf.saved_map_mode = (uint8_t)map_mode;
    save_buf.saved_pad = 0;
    save_buf.rng = rng_state;
    save_buf.seed = campaign_seed;
    save_buf.saved_chron_count = (uint8_t)chron_count;
    for (int i = 0; i < 10; i++) scopy(save_buf.saved_chron[i], chron[i], 64);
    for (int i = 0; i < PROV_MAX; i++) save_buf.prov[i] = prov[i];
    for (int i = 0; i < REALM_MAX; i++) save_buf.realm[i] = realm[i];
    for (int i = 0; i < ARMY_MAX; i++) save_buf.armies[i] = armies[i];
    for (int i = 0; i < WAR_MAX; i++) save_buf.wars[i] = wars[i];
    for (int i = 0; i < REALM_MAX; i++)
        for (int j = 0; j < REALM_MAX; j++) save_buf.truce[i][j] = truce[i][j];
    save_buf.crc = crc32_of((const uint8_t *)&save_buf, save_crc_offset());

    int fd = vfs_create(CK3_SAVE_PATH);
    if (fd < 0) fd = vfs_open(CK3_SAVE_PATH);
    if (fd < 0) return 0;
    return vfs_write(fd, (const uint8_t *)&save_buf,
                     (uint32_t)sizeof(save_buf)) == (int)sizeof(save_buf);
}

static int load_game(void) {
    int fd = vfs_open(CK3_SAVE_PATH);
    if (fd < 0) return 0;
    if (vfs_get_size(fd) != (int)sizeof(save_buf)) return 0;
    if (vfs_read(fd, (uint8_t *)&save_buf, (uint32_t)sizeof(save_buf)) !=
        (int)sizeof(save_buf)) return 0;
    if (save_buf.magic != CK3_SAVE_MAGIC ||
        save_buf.version != CK3_SAVE_VERSION) return 0;
    if (crc32_of((const uint8_t *)&save_buf, save_crc_offset()) != save_buf.crc)
        return 0;

    year = save_buf.saved_year;
    month = save_buf.saved_month;
    player = save_buf.saved_player;
    map_mode = save_buf.saved_map_mode;
    rng_state = save_buf.rng ? save_buf.rng : 0x5EED1066u;
    campaign_seed = save_buf.seed ? save_buf.seed : rng_state;
    chron_count = save_buf.saved_chron_count > 10 ? 10 : save_buf.saved_chron_count;
    for (int i = 0; i < 10; i++) scopy(chron[i], save_buf.saved_chron[i], 64);
    for (int i = 0; i < PROV_MAX; i++) prov[i] = save_buf.prov[i];
    for (int i = 0; i < REALM_MAX; i++) realm[i] = save_buf.realm[i];
    for (int i = 0; i < ARMY_MAX; i++) armies[i] = save_buf.armies[i];
    for (int i = 0; i < WAR_MAX; i++) wars[i] = save_buf.wars[i];
    for (int i = 0; i < REALM_MAX; i++)
        for (int j = 0; j < REALM_MAX; j++) truce[i][j] = save_buf.truce[i][j];

    if (player < 0 || player >= REALM_MAX) player = 0;
    if (year < 1066 || year > 2000) year = 1066;
    if (month < 1 || month > 12) month = 1;
    selected_prov = ck3_realm_defs[player].capital;
    selected_army = -1;
    panel = PANEL_NONE;
    modal = MODAL_NONE;
    ctx_open = 0;
    game_over = 0;
    speed = 0;
    started = 1;
    ticker[0] = '\0';
    return 1;
}

/* A compact, machine-readable state line plus the chronicle, so QEMU can
 * verify a run from the serial log alone. */
static void dump_state(void) {
    char line[80];
    serial_out("CK3STATE");
    scopy(line, " month=", 80); snum(line, abs_month(), 80); serial_out(line);
    scopy(line, " year=", 80); snum(line, year, 80); serial_out(line);
    scopy(line, " seed=", 80); snum(line, (int)(campaign_seed & 0x7FFFFFFFu), 80);
    serial_out(line);
    scopy(line, " player=", 80); snum(line, player, 80); serial_out(line);
    scopy(line, " provinces=", 80); snum(line, realm_provinces(player), 80);
    serial_out(line);
    scopy(line, " gold=", 80); snum(line, realm[player].gold, 80); serial_out(line);
    scopy(line, " levies=", 80); snum(line, realm_levies(player), 80); serial_out(line);
    scopy(line, " hosts=", 80); snum(line, realm_armies(player), 80); serial_out(line);
    scopy(line, " men=", 80); snum(line, realm_host(player), 80); serial_out(line);
    scopy(line, " opinion=", 80); snum(line, realm[player].opinion, 80); serial_out(line);
    scopy(line, " war=", 80); snum(line, realm[player].war, 80); serial_out(line);
    scopy(line, " repaint_ms=", 80); snum(line, repaint_ms, 80); serial_out(line);
    serial_out("\n");
    for (int i = 0; i < chron_count; i++) {
        serial_out("CK3CHRON ");
        serial_out(chron[i]);
        serial_out("\n");
    }
}

/* ------------------------------------------------------------------ */
/* The world                                                           */
/* ------------------------------------------------------------------ */

static int prov_income(int i) {
    int t;
    if (prov[i].occ) return 0;
    t = 1 + prov[i].dev / 2;
    if (prov[i].terrain == T_PLAINS) t++;
    if (prov[i].terrain == T_DESERT) t--;
    return t < 1 ? 1 : t;
}

static int prov_levy_max(int i) {
    static const int bonus[6] = {30, 20, 35, 10, 5, 25};
    return 40 + prov[i].dev * 20 + bonus[prov[i].terrain];
}

static int realm_income(int r) {
    int total = 0;
    for (int i = 0; i < PROV_MAX; i++)
        if (prov[i].owner == r) total += prov_income(i);
    return total;
}

static int realm_levies(int r) {
    int total = 0;
    for (int i = 0; i < PROV_MAX; i++)
        if (prov[i].owner == r && !prov[i].occ) total += prov[i].levy;
    return total;
}

static int realm_provinces(int r) {
    int n = 0;
    for (int i = 0; i < PROV_MAX; i++)
        if (prov[i].owner == r) n++;
    return n;
}

static int realm_armies(int r) {
    int n = 0;
    for (int i = 0; i < ARMY_MAX; i++)
        if (armies[i].alive && armies[i].owner == r) n++;
    return n;
}

static int realm_host(int r) {
    int n = 0;
    for (int i = 0; i < ARMY_MAX; i++)
        if (armies[i].alive && armies[i].owner == r) n += armies[i].men;
    return n;
}

static void prov_refresh(int i) {
    int m = prov_levy_max(i);
    if (prov[i].levy > m) prov[i].levy = (uint16_t)m;
}

static void shell_sort(int32_t *v, int n) {
    static const int gaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
    for (int g = 0; g < 8; g++) {
        int gap = gaps[g];
        for (int i = gap; i < n; i++) {
            int32_t t = v[i];
            int j = i;
            while (j >= gap && v[j - gap] > t) { v[j] = v[j - gap]; j -= gap; }
            v[j] = t;
        }
    }
}

static void add_adj(int a, int b, int sea) {
    if (a == b || a < 0 || b < 0 || a >= PROV_MAX || b >= PROV_MAX) return;
    for (int i = 0; i < adj_n[a]; i++)
        if (adj[a][i] == b) return;
    if (adj_n[a] >= 12) return;
    if (sea) adj_sea[a] |= (uint8_t)(1u << adj_n[a]);
    adj[a][adj_n[a]++] = (uint8_t)b;
    if (adj_n[b] >= 12) return;
    if (sea) adj_sea[b] |= (uint8_t)(1u << adj_n[b]);
    adj[b][adj_n[b]++] = (uint8_t)a;
}

static int adj_is_sea(int a, int i) {
    return (adj_sea[a] >> i) & 1;
}

/* Land spans for one map row, straight from the shore rings. The rings are
 * whole (not clipped to the box) and filled with the even-odd rule, so holes
 * such as the Caspian come out as water. */
#define CROSS_MAX 1400
static int32_t cross_buf[CROSS_MAX];

static void build_world(void) {
    static int16_t col_lon[(CK3_MAP_W + SAMPLE - 1) / SAMPLE];
    static int16_t col_px[(CK3_MAP_W + SAMPLE - 1) / SAMPLE];
    static int32_t sum_x[PROV_MAX];
    static int32_t sum_y[PROV_MAX];
    static uint16_t hits[PROV_MAX];

    if (world_built) return;
    world_built = 1;

    for (int p = 0; p < PROV_MAX; p++) {
        seed_px[p] = (int16_t)(((int)ck3_prov_defs[p].lon - CK3_LON_MIN) *
                               CK3_MAP_W / (CK3_LON_MAX - CK3_LON_MIN));
        seed_py[p] = (int16_t)((CK3_LAT_MAX - (int)ck3_prov_defs[p].lat) *
                               CK3_MAP_H / (CK3_LAT_MAX - CK3_LAT_MIN));
        sum_x[p] = 0;
        sum_y[p] = 0;
        hits[p] = 0;
    }

    for (int c = 0; c < GRID_COLS; c++) {
        int px = c * SAMPLE + SAMPLE / 2;
        col_px[c] = (int16_t)px;
        col_lon[c] = (int16_t)(CK3_LON_MIN + px * (CK3_LON_MAX - CK3_LON_MIN) / CK3_MAP_W);
    }

    for (int row = 0; row < CK3_MAP_H; row++) {
        int y = CK3_LAT_MAX - ((row * 2 + 1) * (CK3_LAT_MAX - CK3_LAT_MIN)) /
                               (CK3_MAP_H * 2);
        int n = 0;

        for (int s = 0; s < CK3_SHORE_COUNT; s++) {
            const struct ck3_geo *pts = ck3_shores[s].pts;
            int cnt = ck3_shores[s].count;
            for (int i = 0; i < cnt; i++) {
                int j = (i + 1 == cnt) ? 0 : i + 1;
                int y0 = pts[i].lat, y1 = pts[j].lat;
                if ((y > y0) == (y > y1)) continue;
                int x0 = pts[i].lon, x1 = pts[j].lon;
                long x = (long)x0 + (long)(y - y0) * (long)(x1 - x0) / (long)(y1 - y0);
                if (n < CROSS_MAX) cross_buf[n++] = (int32_t)x;
            }
        }
        shell_sort(cross_buf, n);

        int span = 0;
        int spans = n / 2;
        for (int c = 0; c < GRID_COLS; c++) {
            int lon = col_lon[c];
            while (span < spans && cross_buf[span * 2 + 1] <= lon) span++;
            if (span >= spans || cross_buf[span * 2] > lon) {
                grid[row][c] = SEAP;
                continue;
            }
            int px = col_px[c];
            int best = 0;
            long bestd = 0x7FFFFFFF;
            for (int p = 0; p < PROV_MAX; p++) {
                long dx = px - seed_px[p];
                long dy = row - seed_py[p];
                long d = dx * dx + dy * dy;
                if (d < bestd) { bestd = d; best = p; }
            }
            grid[row][c] = (uint8_t)best;
            sum_x[best] += px;
            sum_y[best] += row;
            hits[best]++;
        }
    }

    for (int p = 0; p < PROV_MAX; p++) {
        if (hits[p] > 0) {
            prov[p].seat_x = (int16_t)(sum_x[p] / hits[p]);
            prov[p].seat_y = (int16_t)(sum_y[p] / hits[p]);
        } else {
            prov[p].seat_x = seed_px[p];
            prov[p].seat_y = seed_py[p];
        }
    }

    for (int row = 0; row < CK3_MAP_H; row++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int p = grid[row][c];
            if (p == SEAP) continue;
            if (c + 1 < GRID_COLS) {
                int q = grid[row][c + 1];
                if (q != SEAP && q != p) add_adj(p, q, 0);
            }
            if (row + 1 < CK3_MAP_H) {
                int q = grid[row + 1][c];
                if (q != SEAP && q != p) add_adj(p, q, 0);
            }
        }
    }

    /* Islands and narrow straits: a short hop over water is allowed, at the
     * cost of an extra month of travel. */
    for (int a = 0; a < PROV_MAX; a++) {
        for (int b = a + 1; b < PROV_MAX; b++) {
            int dx = (int)seed_px[a] - (int)seed_px[b];
            int dy = (int)seed_py[a] - (int)seed_py[b];
            if (dx * dx + dy * dy < 55 * 55) add_adj(a, b, 1);
        }
    }
}

static void init_colors(void) {
    for (int p = 0; p < PROV_MAX; p++) {
        uint32_t c = ck3_realm_defs[ck3_prov_defs[p].owner].color;
        int terrain = ck3_prov_defs[p].terrain;
        uint32_t base = C_LAND;
        if (terrain == T_DESERT) base = 0xFFE7D2A0;
        if (terrain == T_MOUNTAIN) base = 0xFFC9C2B4;
        if (terrain == T_FOREST) base = 0xFFCFD3AC;
        if (terrain == T_MARSH) base = 0xFFC8CDB6;
        prov_fill[p] = mix_color(c, base, 74);
        prov_edge[p] = mix_color(c, C_INK, 120);
        prov_sel[p] = mix_color(c, 0xFFF8ECC4, 120);
    }
}

static void province_defs_init(void) {
    for (int i = 0; i < PROV_MAX; i++) {
        prov[i].owner = ck3_prov_defs[i].owner;
        prov[i].terrain = ck3_prov_defs[i].terrain;
        prov[i].dev = ck3_prov_defs[i].dev;
        prov[i].fort = (uint8_t)(ck3_prov_defs[i].dev >= 5 ? 2 : 1);
        prov[i].occ = 0;
        prov[i].claim = 0;
        prov[i].levy = (uint16_t)prov_levy_max(i);
    }
    for (int r = 0; r < REALM_MAX; r++) {
        realm[r].opinion = 50;
        realm[r].gold = (int16_t)(60 + rnd_below(80));
        realm[r].prestige = (int16_t)(40 + rnd_below(60));
        realm[r].piety = (int16_t)(20 + rnd_below(50));
        realm[r].alive = 1;
        realm[r].age = (uint8_t)(24 + rnd_below(26));
        realm[r].health = 100;
        realm[r].heir = 1;
        realm[r].heir_age = (uint8_t)(2 + rnd_below(10));
        realm[r].war = 0;
        realm[r].offered = 0;
    }
    for (int i = 0; i < ARMY_MAX; i++) armies[i].alive = 0;
    for (int i = 0; i < WAR_MAX; i++) wars[i].active = 0;
}

/* ------------------------------------------------------------------ */
/* Armies                                                              */
/* ------------------------------------------------------------------ */

static int army_at(int owner, int p) {
    for (int i = 0; i < ARMY_MAX; i++)
        if (armies[i].alive && armies[i].owner == owner && armies[i].prov == p)
            return i;
    return -1;
}

static int army_hostile_at(int p, int owner) {
    for (int i = 0; i < ARMY_MAX; i++) {
        if (!armies[i].alive || armies[i].prov != p || armies[i].owner == owner)
            continue;
        return 1;
    }
    return 0;
}

static int new_army(int owner, int p, int men) {
    for (int i = 0; i < ARMY_MAX; i++) {
        if (armies[i].alive) continue;
        armies[i].alive = 1;
        armies[i].owner = (uint8_t)owner;
        armies[i].prov = (uint8_t)p;
        armies[i].target = 255;
        armies[i].next = 255;
        armies[i].delay = 0;
        armies[i].morale = 100;
        armies[i].siege = 0;
        armies[i].men = (uint16_t)men;
        return i;
    }
    return -1;
}

static int war_between(int a, int b) {
    for (int i = 0; i < WAR_MAX; i++) {
        if (!wars[i].active) continue;
        if (wars[i].attacker == a && wars[i].defender == b) return i;
        if (wars[i].attacker == b && wars[i].defender == a) return i;
    }
    return -1;
}

/* Shortest path over the province graph. Land steps cost one month, sea
 * crossings two; returns the first province to march into. */
static int path_next(int from, int to) {
    static uint8_t dist[PROV_MAX];
    static uint8_t prev[PROV_MAX];
    static uint8_t done[PROV_MAX];
    if (from == to) return 255;
    for (int i = 0; i < PROV_MAX; i++) { dist[i] = 255; prev[i] = 255; done[i] = 0; }
    dist[from] = 0;
    for (;;) {
        int u = -1;
        int best = 255;
        for (int i = 0; i < PROV_MAX; i++)
            if (!done[i] && dist[i] < best) { best = dist[i]; u = i; }
        if (u < 0) break;
        if (u == to) break;
        done[u] = 1;
        for (int k = 0; k < adj_n[u]; k++) {
            int v = adj[u][k];
            int cost = adj_is_sea(u, k) ? 2 : 1;
            if (dist[u] + cost < dist[v]) {
                dist[v] = (uint8_t)(dist[u] + cost);
                prev[v] = (uint8_t)u;
            }
        }
    }
    if (dist[to] == 255) return 255;
    int cur = to;
    while (prev[cur] != 255 && prev[cur] != (uint8_t)from) cur = prev[cur];
    return prev[cur] == (uint8_t)from ? cur : 255;
}

static void order_march(int a, int target) {
    if (a < 0 || a >= ARMY_MAX || !armies[a].alive) return;
    if (target < 0 || target >= PROV_MAX) return;
    if (target == armies[a].prov) { armies[a].target = 255; return; }
    armies[a].target = (uint8_t)target;
    armies[a].next = (uint8_t)path_next(armies[a].prov, target);
}

static int army_slot_selected(void) {
    if (selected_army >= 0 && selected_army < ARMY_MAX && armies[selected_army].alive)
        return selected_army;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Wars                                                                */
/* ------------------------------------------------------------------ */

static void war_add_score(int r, int delta) {
    int idx = realm[r].war;
    if (!idx) return;
    idx--;
    if (wars[idx].attacker == r) wars[idx].score += (int16_t)delta;
    else wars[idx].score -= (int16_t)delta;
    if (wars[idx].score > 150) wars[idx].score = 150;
    if (wars[idx].score < -150) wars[idx].score = -150;
}

static void clear_occupations(int a, int b) {
    for (int i = 0; i < PROV_MAX; i++) {
        int occ = prov[i].occ ? prov[i].occ - 1 : -1;
        if (occ == a || occ == b) prov[i].occ = 0;
    }
}

static void end_war(int wi, int victor) {
    struct war_state *w = &wars[wi];
    int attacker = w->attacker;
    int defender = w->defender;
    int target = w->target;
    int casus = w->casus;
    char line[64];

    if (!w->active) return;
    w->active = 0;
    realm[attacker].war = 0;
    realm[defender].war = 0;
    realm[attacker].offered = 0;
    realm[defender].offered = 0;
    clear_occupations(attacker, defender);

    /* Both sides are bound for two years. Without this the AI re-declares the
     * month after signing and a peace looks broken from the player's side. */
    {
        int until = abs_month() + TRUCE_MONTHS;
        if (until > 65000) until = 65000;
        truce[attacker][defender] = (uint16_t)until;
        truce[defender][attacker] = (uint16_t)until;
    }

    if (victor == attacker) {
        if (casus == CB_RAID) {
            realm[attacker].gold = (int16_t)(realm[attacker].gold + 70);
            realm[attacker].prestige = (int16_t)(realm[attacker].prestige + 40);
            realm[defender].gold = (int16_t)imax(0, realm[defender].gold - 40);
            chron_add("The raid is a success; the host comes home laden.");
        } else if (prov[target].owner == defender) {
            prov[target].owner = (uint8_t)attacker;
            prov[target].occ = 0;
            prov[target].claim = 0;
            prov_refresh(target);
            realm[attacker].prestige = (int16_t)(realm[attacker].prestige + 120);
            scopy(line, ck3_prov_defs[target].name, 64);
            sappend(line, " joins your realm.", 64);
            chron_add(line);
        }
    } else if (victor == defender) {
        realm[defender].prestige = (int16_t)(realm[defender].prestige + 100);
        realm[attacker].prestige = (int16_t)imax(0, realm[attacker].prestige - 80);
        realm[attacker].gold = (int16_t)imax(0, realm[attacker].gold - 40);
        if ((casus != CB_RAID) && prov[target].owner == attacker) {
            prov[target].owner = (uint8_t)defender;
            prov[target].occ = 0;
            prov_refresh(target);
            scopy(line, "The war ends: ", 64);
            sappend(line, ck3_prov_defs[target].name, 64);
            sappend(line, " is lost.", 64);
            chron_add(line);
        } else {
            chron_add("The enemy host is beaten back. Peace returns.");
        }
    } else {
        chron_add("A white peace is signed. The borders hold.");
    }

    if (realm_provinces(attacker) == 0 || realm_provinces(defender) == 0) {
        int gone = realm_provinces(attacker) == 0 ? attacker : defender;
        realm[gone].alive = 0;
    }
}

static int declare_war(int attacker, int defender, int target, int casus) {
    int slot = -1;
    char line[64];
    if (attacker == defender) return -1;
    if (realm[attacker].war || realm[defender].war) return -1;
    if (truce[attacker][defender] > (uint16_t)abs_month()) return -1;
    for (int i = 0; i < WAR_MAX; i++)
        if (!wars[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    wars[slot].active = 1;
    wars[slot].attacker = (uint8_t)attacker;
    wars[slot].defender = (uint8_t)defender;
    wars[slot].target = (uint8_t)target;
    wars[slot].casus = (uint8_t)casus;
    wars[slot].score = 0;
    wars[slot].months = 0;
    realm[attacker].war = (uint8_t)(slot + 1);
    realm[defender].war = (uint8_t)(slot + 1);

    if (attacker == player || defender == player) {
        char tmp[64];
        scopy(tmp, "War on ", 64);
        sappend(tmp, ck3_realm_defs[defender].label, 64);
        sappend(tmp, ": ", 64);
        sappend(tmp, casus_name(casus), 64);
        sappend(tmp, ".", 64);
        chron_add(tmp);
    } else {
        scopy(line, ck3_realm_defs[attacker].label, 64);
        sappend(line, " makes war on ", 64);
        sappend(line, ck3_realm_defs[defender].label, 64);
        chron_add(line);
        /* Foreign wars get a line of their own in the header: the AI is
         * only as convincing as the player's ability to see what it does. */
        scopy(ticker, line, 64);
    }
    return slot;
}

/* ------------------------------------------------------------------ */
/* Battle and siege                                                    */
/* ------------------------------------------------------------------ */

static int terrain_defence(int t) {
    switch (t) {
        case T_HILLS: return 18;
        case T_MOUNTAIN: return 35;
        case T_MARSH: return 12;
        case T_FOREST: return 10;
        default: return 0;
    }
}

static int retreat_province(int owner, int from) {
    for (int k = 0; k < adj_n[from]; k++) {
        int v = adj[from][k];
        if (prov[v].owner == owner) return v;
    }
    for (int p = 0; p < PROV_MAX; p++)
        if (prov[p].owner == owner) return p;
    return -1;
}

static void resolve_battle(int ai, int bi) {
    struct army_state *a = &armies[ai];
    struct army_state *b = &armies[bi];
    int proto = prov[a->prov].terrain;
    int bonus = terrain_defence(proto);
    int sa = a->men * (50 + a->morale) / 100;
    int sb = b->men * (50 + b->morale) / 100;
    sa = sa * (80 + rnd_below(41)) / 100;
    sb = sb * (100 + bonus) / 100 * (80 + rnd_below(41)) / 100;

    int a_lost, b_lost, winner, loser;
    if (sa >= sb) {
        winner = a->owner;
        loser = b->owner;
        a_lost = a->men / 8 + rnd_below(a->men / 6 + 1);
        b_lost = b->men / 3 + rnd_below(b->men / 4 + 1);
    } else {
        winner = b->owner;
        loser = a->owner;
        b_lost = b->men / 8 + rnd_below(b->men / 6 + 1);
        a_lost = a->men / 3 + rnd_below(a->men / 4 + 1);
    }
    a->men = (uint16_t)imax(0, a->men - a_lost);
    b->men = (uint16_t)imax(0, b->men - b_lost);

    int lose_i = (winner == a->owner) ? bi : ai;
    int win_i = (winner == a->owner) ? ai : bi;
    armies[win_i].morale = (uint8_t)imin(100, armies[win_i].morale + 6);
    war_add_score(winner, 10 + rnd_below(8));
    war_add_score(loser, -4);

    if (armies[lose_i].men < 180) {
        armies[lose_i].alive = 0;
    } else {
        armies[lose_i].morale = (uint8_t)(armies[lose_i].morale > 25
                                             ? armies[lose_i].morale - 22 : 10);
        int ref = retreat_province(loser, armies[lose_i].prov);
        if (ref < 0) {
            armies[lose_i].alive = 0;
        } else {
            armies[lose_i].prov = (uint8_t)ref;
            armies[lose_i].target = 255;
            armies[lose_i].siege = 0;
        }
    }

    char line[64];
    scopy(line, "Battle at ", 64);
    sappend(line, ck3_prov_defs[armies[win_i].prov].name, 64);
    sappend(line, ": ", 64);
    sappend(line, ck3_realm_defs[winner].label, 64);
    sappend(line, " wins.", 64);
    chron_add(line);

    if (selected_army >= 0 && !armies[selected_army].alive) selected_army = -1;
}

static void run_battles(void) {
    for (int p = 0; p < PROV_MAX; p++) {
        int found_a = -1, found_b = -1;
        for (int i = 0; i < ARMY_MAX; i++) {
            if (!armies[i].alive || armies[i].prov != p) continue;
            if (found_a < 0) { found_a = i; continue; }
            if (war_between(armies[found_a].owner, armies[i].owner) >= 0) {
                found_b = i;
                break;
            }
        }
        if (found_a >= 0 && found_b >= 0) resolve_battle(found_a, found_b);
    }
}

static void occupy_province(int p, int occupier) {
    char line[64];
    prov[p].occ = (uint8_t)(occupier + 1);
    war_add_score(occupier, 5 + prov[p].dev * 3);
    scopy(line, ck3_prov_defs[p].name, 64);
    sappend(line, " falls to ", 64);
    sappend(line, ck3_realm_defs[occupier].label, 64);
    sappend(line, ".", 64);
    chron_add(line);
    if (prov[p].owner == player) realm[player].prestige =
        (int16_t)imax(0, realm[player].prestige - 15);
}

static void run_sieges(void) {
    for (int i = 0; i < ARMY_MAX; i++) {
        struct army_state *a = &armies[i];
        if (!a->alive) continue;
        int p = a->prov;
        int owner = prov[p].owner;
        if (owner == a->owner) {
            if (prov[p].occ && prov[p].occ - 1 != a->owner) {
                prov[p].occ = 0;
                chron_add("Your host recovers an occupied province.");
            }
            continue;
        }
        if (war_between(a->owner, owner) < 0) continue;
        if (army_hostile_at(p, a->owner)) continue;
        int gain = 10 + a->men / 500 + rnd_below(10) - prov[p].fort * 9;
        if (gain < 1) gain = 1;
        a->siege = (uint8_t)imin(200, a->siege + gain);
        if (a->siege >= 100) {
            a->siege = 0;
            occupy_province(p, a->owner);
        }
    }
}

/* ------------------------------------------------------------------ */
/* AI                                                                  */
/* ------------------------------------------------------------------ */

static int realm_weakest_neighbour(int r, int *target_out) {
    int best = -1;
    long best_score = 0x7FFFFFFF;
    for (int p = 0; p < PROV_MAX; p++) {
        if (prov[p].owner != r) continue;
        for (int k = 0; k < adj_n[p]; k++) {
            int q = adj[p][k];
            int other = prov[q].owner;
            if (other == r) continue;
            if (!realm[other].alive) continue;
            if (realm[other].war) continue;
            if (war_between(r, other) >= 0) continue;
            long score = (long)realm_provinces(other) * 100 + realm_host(other) / 8;
            if (prov[q].fort > 1) score += 40;
            if (score < best_score) { best_score = score; best = q; }
        }
    }
    *target_out = best;
    return best >= 0 ? prov[best].owner : -1;
}

static void ai_month(int r) {
    if (r == player || !realm[r].alive) return;

    if (realm[r].war) {
        int wi = realm[r].war - 1;
        int target = wars[wi].target;
        int slot = -1;
        for (int i = 0; i < ARMY_MAX; i++)
            if (armies[i].alive && armies[i].owner == r) { slot = i; break; }
        if (slot < 0 && realm[r].gold >= 25) {
            int home = -1;
            for (int p = 0; p < PROV_MAX; p++)
                if (prov[p].owner == r && prov[p].levy >= 60) { home = p; break; }
            if (home >= 0) {
                int men = prov[home].levy;
                prov[home].levy = 0;
                realm[r].gold = (int16_t)(realm[r].gold - 25);
                slot = new_army(r, home, men);
                if (slot >= 0) order_march(slot, target);
            }
        } else if (slot >= 0 && armies[slot].target == 255) {
            if (wars[wi].attacker == r) order_march(slot, target);
            else {
                int home = -1;
                for (int p = 0; p < PROV_MAX; p++)
                    if (prov[p].owner == r && (home < 0 || prov[p].fort > prov[home].fort))
                        home = p;
                if (home >= 0) order_march(slot, home);
            }
        }
        return;
    }

    int want = ck3_realm_defs[r].aggression;
    if (rnd_below(100) >= want / 9) return;

    int target = -1;
    int victim = realm_weakest_neighbour(r, &target);
    if (victim < 0 || target < 0) return;
    if (rnd_below(100) > 55) return;
    int casus = (ck3_realm_defs[r].faith != ck3_realm_defs[victim].faith &&
                 realm[r].piety >= 50) ? CB_HOLY : CB_CONQUEST;
    declare_war(r, victim, target, casus);
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

struct event_def {
    const char *title;
    const char *text;
    const char *opt1;
    const char *opt2;
};

static const struct event_def events[] = {
    {"A merchant of Brabant",
     "offers a loan at a steep rate, and waits for your answer.",
     "Take 60 gold (-10 prestige)", "Decline politely"},
    {"The bishop requests",
     "a donation for the new abbey. He watches your purse.",
     "Give 40 gold (+35 piety)", "Refuse (-5 piety)"},
    {"A distant claimant",
     "disputes your title to a province. Your steward looks worried.",
     "Pay 30 gold (+15 prestige)", "Ignore him (-1 development)"},
    {"The harvest is rich",
     "and the villages bring tribute to your hall.",
     "Hold a feast (+25 prestige)", "Store the grain (+45 gold)"},
    {"Your marshal drills",
     "the levies in the courtyard. The men are willing.",
     "Pay 40 gold (levies grow)", "Spare the treasury"},
    {"A neighbour proposes",
     "a marriage alliance between your houses.",
     "Accept (+30 prestige, +20 gold)", "Refuse (-5 prestige)"},
    {"Your council demands",
     "a seat for the great vassals, and will not be put off.",
     "Grant it (-30 gold, +20 prestige)", "Refuse (-15 prestige)"},
    {"A pretender gathers",
     "men in the hills and calls on your vassals to join him.",
     "Pay him off (50 gold)", "Let him come (-1 development)"},
    {"A relic is offered",
     "to your chapel by a wandering monk.",
     "Buy it (70 gold, +60 piety)", "Send him away"},
    {"The harvest fails",
     "and the granaries are nearly empty.",
     "Buy grain (30 gold)", "Let the people starve (-1 development)"},
    {"A rival sends an ultimatum:",
     "tribute, or his host will visit your lands.",
     "Pay 40 gold", "Refuse (he may declare war)"},
    {"Pilgrims ask for shelter",
     "on their road to the holy places.",
     "Feed them (20 gold, +25 piety)", "Turn them away"},
};

#define EVENT_COUNT ((int)(sizeof(events) / sizeof(events[0])))

static void player_event_apply(int kind, int choice, int arg);

static void open_event(int kind, int arg) {
    event_kind = kind;
    modal_arg = arg;
    modal = MODAL_EVENT;
    speed = 0;
}

/* ------------------------------------------------------------------ */
/* Player actions                                                      */
/* ------------------------------------------------------------------ */

static void act_feast(void) {
    if (realm[player].gold < 30) { chron_add("You cannot afford a feast."); return; }
    realm[player].gold = (int16_t)(realm[player].gold - 30);
    realm[player].prestige = (int16_t)(realm[player].prestige + 25);
    if (realm[player].health < 95) realm[player].health += 5;
    chron_add("Your court feasts and talks of you.");
}

static void act_pilgrimage(void) {
    if (realm[player].gold < 50) { chron_add("A pilgrimage costs 50 gold."); return; }
    realm[player].gold = (int16_t)(realm[player].gold - 50);
    realm[player].piety = (int16_t)(realm[player].piety + 40);
    chron_add("You walk to the shrine and return.");
}

static void act_marry(void) {
    if (realm[player].heir) { chron_add("You already have an heir."); return; }
    if (realm[player].gold < 40) { chron_add("A marriage costs 40 gold."); return; }
    realm[player].gold = (int16_t)(realm[player].gold - 40);
    realm[player].heir = 1;
    realm[player].heir_age = 0;
    chron_add("A child is born to your house.");
}

static void act_build(int p) {
    if (prov[p].owner != player) { chron_add("Build only in your own provinces."); return; }
    if (prov[p].dev >= MAX_DEV) { chron_add("It is fully built."); return; }
    if (realm[player].gold < 60) { chron_add("You need 60 gold to build."); return; }
    realm[player].gold = (int16_t)(realm[player].gold - 60);
    prov[p].dev++;
    prov_refresh(p);
    char line[64];
    scopy(line, "Built up ", 64);
    sappend(line, ck3_prov_defs[p].name, 64);
    chron_add(line);
}

static void act_fortify(int p) {
    if (prov[p].owner != player) { chron_add("Fortify only your own land."); return; }
    if (prov[p].fort >= MAX_FORT) { chron_add("The walls cannot be raised higher."); return; }
    if (realm[player].gold < 80) { chron_add("New walls cost 80 gold."); return; }
    realm[player].gold = (int16_t)(realm[player].gold - 80);
    prov[p].fort++;
    char line[64];
    scopy(line, "New walls rise at ", 64);
    sappend(line, ck3_prov_defs[p].name, 64);
    chron_add(line);
}

static void act_raise(int p) {
    if (prov[p].owner != player) { chron_add("Raise levies only in your own land."); return; }
    if (prov[p].occ) { chron_add("The province is occupied."); return; }
    if (realm[player].gold < 20) { chron_add("Mustering the levies costs 20 gold."); return; }
    if (prov[p].levy < 60) { chron_add("Too few men remain to be raised here."); return; }
    if (army_at(player, p) >= 0) { chron_add("A host already musters here."); return; }
    int men = prov[p].levy;
    prov[p].levy = 0;
    realm[player].gold = (int16_t)(realm[player].gold - 20);
    int slot = new_army(player, p, men);
    if (slot < 0) {
        prov[p].levy = (uint16_t)men;
        realm[player].gold = (int16_t)(realm[player].gold + 20);
        chron_add("No free command remains for another host.");
        return;
    }
    selected_army = slot;
    panel = PANEL_ARMY;
    char line[64];
    scopy(line, "The levies of ", 64);
    sappend(line, ck3_prov_defs[p].name, 64);
    sappend(line, " muster.", 64);
    chron_add(line);
}

static void act_fabricate(int p) {
    if (prov[p].owner == player) { chron_add("You already hold that province."); return; }
    if (prov[p].claim) { chron_add("Your chancery already has a claim."); return; }
    if (realm[player].gold < 40) { chron_add("The chancery wants 40 gold."); return; }
    realm[player].gold = (int16_t)(realm[player].gold - 40);
    if (rnd_below(100) < 55 + realm[player].prestige / 8) {
        prov[p].claim = 1;
        char line[64];
        scopy(line, "A claim on ", 64);
        sappend(line, ck3_prov_defs[p].name, 64);
        sappend(line, " is sealed.", 64);
        chron_add(line);
    } else {
        chron_add("The chancery finds nothing. The gold is spent.");
    }
}

/* ------------------------------------------------------------------ */
/* Monthly turn                                                        */
/* ------------------------------------------------------------------ */

static void check_succession(void) {
    if (realm[player].heir) {
        realm[player].heir = 0;
        realm[player].heir_age = 0;
        realm[player].age = 16;
        realm[player].health = 100;
        realm[player].prestige = (int16_t)(realm[player].prestige * 4 / 5);
        realm[player].opinion = 60;   /* a new lord is given the benefit */
        chron_add("You die; your heir takes the seat.");
    } else {
        game_over = 1;
        speed = 0;
        chron_add("You die without an heir. The dynasty ends.");
    }
}

static void advance_month(void) {
    char line[64];
    if (game_over) return;

    month++;
    if (month > 12) { month = 1; year++; }

    for (int r = 0; r < REALM_MAX; r++) {
        if (!realm[r].alive) continue;
        realm[r].gold = (int16_t)(realm[r].gold + realm_income(r));
        if (realm[r].gold > 32000) realm[r].gold = 32000;
        /* Vassals drift toward their ruler's standing: prestige wins them
         * over, a long war loses them. */
        {
            int target = 35 + realm[r].prestige / 6 - (realm[r].war ? 18 : 0);
            if (target < 0) target = 0;
            if (target > 100) target = 100;
            if (realm[r].opinion < target) realm[r].opinion++;
            else if (realm[r].opinion > target) realm[r].opinion--;
        }
        for (int p = 0; p < PROV_MAX; p++) {
            if (prov[p].owner != r) continue;
            int max = prov_levy_max(p);
            /* Restless vassals send fewer men. */
            int regen = 6 + prov[p].dev + (realm[r].opinion - 50) / 20;
            if (regen < 2) regen = 2;
            if (prov[p].levy < max)
                prov[p].levy = (uint16_t)imin(max, prov[p].levy + regen);
        }
        if (realm_armies(r) > 0) {
            int upkeep = realm_host(r) / 400 + 1;
            realm[r].gold = (int16_t)(realm[r].gold - upkeep);
            if (realm[r].gold < 0) {
                realm[r].gold = 0;
                for (int i = 0; i < ARMY_MAX; i++)
                    if (armies[i].alive && armies[i].owner == r &&
                        armies[i].morale > 30)
                        armies[i].morale -= 10;
            }
        }
    }

    /* Marching hosts. */
    for (int i = 0; i < ARMY_MAX; i++) {
        struct army_state *a = &armies[i];
        if (!a->alive) continue;
        if (a->delay > 0) { a->delay--; continue; }
        if (a->target == 255) continue;
        if (a->prov == a->target) { a->target = 255; a->next = 255; continue; }
        if (a->next == 255) {
            a->next = (uint8_t)path_next(a->prov, a->target);
            if (a->next == 255) continue;
        }
        int from = a->prov;
        int to = a->next;
        int sea = 0;
        for (int k = 0; k < adj_n[from]; k++)
            if (adj[from][k] == to && adj_is_sea(from, k)) sea = 1;
        a->prov = (uint8_t)to;
        a->siege = 0;
        a->delay = (uint8_t)(sea ? 1 : 0);
        if (a->prov == a->target) { a->target = 255; a->next = 255; }
        else a->next = (uint8_t)path_next(a->prov, a->target);
    }

    run_battles();
    run_sieges();

    for (int r = 0; r < REALM_MAX; r++) {
        if (!realm[r].alive) continue;
        if (realm[r].war) {
            wars[realm[r].war - 1].months++;
            int wi = realm[r].war - 1;
            if (wars[wi].score >= 100) end_war(wi, wars[wi].attacker);
            else if (wars[wi].score <= -100) end_war(wi, wars[wi].defender);
        }
    }
    for (int w = 0; w < WAR_MAX; w++) {
        if (!wars[w].active) continue;
        if (wars[w].attacker == player || wars[w].defender == player) continue;
        if (wars[w].months > 30 + rnd_below(30)) {
            int victor = wars[w].score >= 40 ? wars[w].attacker
                        : wars[w].score <= -40 ? wars[w].defender : -1;
            end_war(w, victor);
        }
    }

    for (int r = 0; r < REALM_MAX; r++) ai_month(r);

    /* A war the player is losing may bring an offer of peace. */
    if (!modal && !game_over && realm[player].war) {
        int wi = realm[player].war - 1;
        struct war_state *w = &wars[wi];
        int side = w->attacker == player ? 1 : -1;
        int mine = w->score * side;
        if (w->months > 6 && !realm[w->defender].offered) {
            if (mine <= -55 && rnd_below(100) < 35) {
                realm[w->defender].offered = 1;
                open_event(100, wi);
            } else if (mine >= 55 && rnd_below(100) < 30) {
                realm[w->attacker].offered = 1;
                open_event(101, wi);
            }
        }
    }

    if (!modal && !game_over && rnd_below(100) < 9) {
        open_event(rnd_below(EVENT_COUNT), 0);
    }

    if (realm[player].heir && realm[player].heir_age < 40) realm[player].heir_age++;
    if (month == 1) {
        realm[player].age++;
        /* Autosave every January so a campaign outlives the window. */
        if (!game_over) save_game();
    }
    int risk = 0;
    if (realm[player].age > 45) risk = (realm[player].age - 45) * 2;
    if (realm[player].health < 60) risk += 4;
    if (risk > 0 && rnd_below(1200) < risk) check_succession();

    if (realm_provinces(player) == 0 && !game_over) {
        game_over = 1;
        speed = 0;
        scopy(line, "Your last province is lost. The dynasty ends.", 64);
        chron_add(line);
    }
}

/* ------------------------------------------------------------------ */
/* Casus belli and events                                              */
/* ------------------------------------------------------------------ */

static const char *const casus_label[CB_COUNT] = {
    "Press a claim", "Holy war", "Conquest", "Border raid"
};
static const char *const casus_why[CB_COUNT] = {
    "Your chancery holds a sealed claim on this province (25 prestige).",
    "They follow another faith, and the church blesses the war (75 piety).",
    "Your host will simply take the land by force (50 prestige).",
    "A cheap raid for plunder and prestige. No land changes hands."
};

static const char *casus_name(int cb) {
    if (cb < 0 || cb >= CB_COUNT) return "war";
    return casus_label[cb];
}

static void player_event_apply(int kind, int choice, int arg) {
    int take;
    char line[64];

    if (kind == 100) {
        if (choice == 0 && arg >= 0 && arg < WAR_MAX && wars[arg].active) {
            int victor = wars[arg].attacker == player ? wars[arg].defender
                                                      : wars[arg].attacker;
            end_war(arg, victor);
            chron_add("You accept peace on the enemy's terms.");
        } else {
            chron_add("You will not treat with them yet.");
        }
        return;
    }
    if (kind == 101) {
        if (choice == 0 && arg >= 0 && arg < WAR_MAX && wars[arg].active) {
            end_war(arg, player);
            chron_add("Peace is signed on your terms.");
        } else {
            chron_add("The war goes on. You demand more.");
        }
        return;
    }

    switch (kind) {
        case 0:
            if (choice == 0) {
                realm[player].gold = (int16_t)(realm[player].gold + 60);
                realm[player].prestige = (int16_t)imax(0, realm[player].prestige - 10);
                chron_add("You take the merchant's loan.");
            } else {
                chron_add("You decline the loan.");
            }
            break;
        case 1:
            if (choice == 0) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 40);
                realm[player].piety = (int16_t)(realm[player].piety + 35);
                chron_add("The abbey remembers your gift.");
            } else {
                realm[player].piety = (int16_t)imax(0, realm[player].piety - 5);
                chron_add("The bishop leaves disappointed.");
            }
            break;
        case 2:
            if (choice == 0) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 30);
                realm[player].prestige = (int16_t)(realm[player].prestige + 15);
                chron_add("The claimant withdraws his suit.");
            } else {
                realm[player].prestige = (int16_t)imax(0, realm[player].prestige - 20);
                for (int i = 0; i < PROV_MAX; i++)
                    if (prov[i].owner == player && prov[i].dev > 1) {
                        prov[i].dev--;
                        prov_refresh(i);
                        break;
                    }
                chron_add("Your title is mocked at court.");
            }
            break;
        case 3:
            if (choice == 0) {
                realm[player].prestige = (int16_t)(realm[player].prestige + 25);
                chron_add("The feast is long remembered.");
            } else {
                realm[player].gold = (int16_t)(realm[player].gold + 45);
                chron_add("The granaries fill, and coin follows.");
            }
            break;
        case 4:
            if (choice == 0) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 40);
                for (int i = 0; i < PROV_MAX; i++)
                    if (prov[i].owner == player) {
                        int max = prov_levy_max(i);
                        prov[i].levy = (uint16_t)imin(max, prov[i].levy + 120);
                    }
                chron_add("The levies are drilled and ready.");
            } else {
                chron_add("You spare the treasury.");
            }
            break;
        case 5:
            if (choice == 0) {
                realm[player].prestige = (int16_t)(realm[player].prestige + 30);
                realm[player].gold = (int16_t)(realm[player].gold + 20);
                chron_add("A marriage alliance is sworn.");
            } else {
                realm[player].prestige = (int16_t)imax(0, realm[player].prestige - 5);
                chron_add("The proposal is politely refused.");
            }
            break;
        case 6:
            if (choice == 0) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 30);
                realm[player].prestige = (int16_t)(realm[player].prestige + 20);
                chron_add("The council is seated and content.");
            } else {
                realm[player].prestige = (int16_t)imax(0, realm[player].prestige - 15);
                chron_add("The great vassals mutter in the hall.");
            }
            break;
        case 7:
            if (choice == 0) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 50);
                chron_add("The pretender takes your silver and goes.");
            } else {
                for (int i = 0; i < PROV_MAX; i++)
                    if (prov[i].owner == player && prov[i].dev > 1) {
                        prov[i].dev--;
                        prov_refresh(i);
                        break;
                    }
                realm[player].prestige = (int16_t)imax(0, realm[player].prestige - 10);
                chron_add("The pretender burns a village and vanishes.");
            }
            break;
        case 8:
            if (choice == 0 && realm[player].gold >= 70) {
                realm[player].gold = (int16_t)(realm[player].gold - 70);
                realm[player].piety = (int16_t)(realm[player].piety + 60);
                chron_add("The relic shines in your chapel.");
            } else {
                chron_add("The monk moves on with his relic.");
            }
            break;
        case 9:
            if (choice == 0) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 30);
                chron_add("The grain ships arrive in time.");
            } else {
                for (int i = 0; i < PROV_MAX; i++)
                    if (prov[i].owner == player && prov[i].dev > 1) {
                        prov[i].dev--;
                        prov_refresh(i);
                        break;
                    }
                realm[player].prestige = (int16_t)imax(0, realm[player].prestige - 10);
                chron_add("The people starve, and remember.");
            }
            break;
        case 10:
            if (choice == 0) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 40);
                chron_add("You pay the tribute, and keep the peace.");
            } else {
                take = -1;
                for (int r = 0; r < REALM_MAX; r++)
                    if (r != player && realm[r].alive && !realm[r].war &&
                        realm_provinces(r) > realm_provinces(player))
                        take = r;
                if (take >= 0 && rnd_below(100) < 55) {
                    int target = -1;
                    for (int i = 0; i < PROV_MAX; i++)
                        if (prov[i].owner == player) { target = i; break; }
                    if (target >= 0) declare_war(take, player, target, CB_CONQUEST);
                    chron_add("He answers your refusal with war.");
                } else {
                    realm[player].prestige = (int16_t)(realm[player].prestige + 20);
                    chron_add("He blusters, then rides away.");
                }
            }
            break;
        case 11:
            if (choice == 0 && realm[player].gold >= 20) {
                realm[player].gold = (int16_t)(realm[player].gold - 20);
                realm[player].piety = (int16_t)(realm[player].piety + 25);
                chron_add("The pilgrims bless your name.");
            } else {
                realm[player].prestige = (int16_t)imax(0, realm[player].prestige - 5);
                chron_add("The pilgrims are turned away into the rain.");
            }
            break;
        default:
            break;
    }

    if (realm[player].gold < 0) realm[player].gold = 0;
    if (realm[player].piety < 0) realm[player].piety = 0;
    if (realm[player].prestige < 0) realm[player].prestige = 0;
    (void)line;
}

/* ------------------------------------------------------------------ */
/* Decisions                                                           */
/* ------------------------------------------------------------------ */

#define DEC_COUNT 7
static const char *const dec_title[DEC_COUNT] = {
    "Hold a feast", "Go on pilgrimage", "Develop province",
    "Fortify province", "Raise levies", "Fabricate a claim",
    "Marry and beget an heir"
};
static const int dec_cost[DEC_COUNT] = {30, 50, 60, 80, 20, 40, 40};

static int dec_enabled(int i) {
    int p = selected_prov;
    switch (i) {
        case 0: return realm[player].gold >= 30;
        case 1: return realm[player].gold >= 50;
        case 2: return prov[p].owner == player && prov[p].dev < MAX_DEV &&
                       realm[player].gold >= 60;
        case 3: return prov[p].owner == player && prov[p].fort < MAX_FORT &&
                       realm[player].gold >= 80;
        case 4: return prov[p].owner == player && !prov[p].occ &&
                       prov[p].levy >= 60 && realm[player].gold >= 20 &&
                       army_at(player, p) < 0;
        case 5: return prov[p].owner != player && !prov[p].claim &&
                       realm[player].gold >= 40;
        default: return !realm[player].heir && realm[player].gold >= 40;
    }
}

static void dec_run(int i) {
    if (!dec_enabled(i)) return;
    switch (i) {
        case 0: act_feast(); break;
        case 1: act_pilgrimage(); break;
        case 2: act_build(selected_prov); break;
        case 3: act_fortify(selected_prov); break;
        case 4: act_raise(selected_prov); break;
        case 5: act_fabricate(selected_prov); break;
        default: act_marry(); break;
    }
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */

struct layout {
    int x, y, w, h;
    int head_h;
    int bar_h;
    int map_x, map_y, map_w, map_h;
    int ruler_x, ruler_y, ruler_w, ruler_h;
    int tab_x, tab_y, tab_w, tab_h;
    int panel_x, panel_y, panel_w, panel_h;
    int prov_x, prov_y, prov_w, prov_h;
    int speed_x, speed_y, speed_w, speed_h;
    int next_x, next_y, next_w, next_h;
};

static void compute_layout(int x, int y, int w, int h, struct layout *L) {
    L->x = x; L->y = y; L->w = w; L->h = h;
    L->head_h = 42;   /* two rows: title/date and the world ticker */
    L->bar_h = h > 460 ? 102 : 88;
    L->map_x = x + 4;
    L->map_y = y + L->head_h + 4;
    L->map_w = w - 8;
    L->map_h = h - L->head_h - L->bar_h - 8;
    if (L->map_h < 120) L->map_h = 120;

    L->ruler_x = x + 8;
    L->ruler_y = y + h - L->bar_h + 6;
    L->ruler_w = 232;
    L->ruler_h = L->bar_h - 12;

    L->tab_x = L->ruler_x + L->ruler_w + 8;
    L->tab_y = L->ruler_y;
    L->tab_w = 90;
    L->tab_h = 26;

    L->panel_x = L->ruler_x;
    L->panel_y = L->map_y + 6;
    L->panel_w = 300;
    L->panel_h = L->map_h - 12;
    if (L->panel_h > 330) L->panel_h = 330;
    if (L->panel_w > w - 20) L->panel_w = w - 20;

    L->prov_w = 260;
    L->prov_x = x + w - L->prov_w - 8;
    L->prov_y = L->map_y + 6;
    L->prov_h = 148;

    L->speed_x = x + w - 250;
    L->speed_y = y + 5;
    L->speed_w = 26;
    L->speed_h = 20;

    L->next_w = 118;
    L->next_h = 20;
    L->next_x = x + w - L->next_w - 8;
    L->next_y = y + 5;
}

static int in_box(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color);
}

static void frame(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    graphics_draw_rect((uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
                       (uint32_t)w, (uint32_t)h, color);
}

static void txt(int x, int y, const char *s, uint32_t color) {
    if (!s || !s[0]) return;
    if (x < 0 || y < 0) return;
    if (x >= (int)graphics_get_width() || y >= (int)graphics_get_height() - 8) return;
    graphics_draw_string((uint32_t)x, (uint32_t)y, s, color);
}

static void txt_center(int cx, int y, const char *s, uint32_t color) {
    txt(cx - slen(s) * 4, y, s, color);
}

static void txt_right(int rx, int y, const char *s, uint32_t color) {
    txt(rx - slen(s) * 8, y, s, color);
}

static void txt_shadow(int x, int y, const char *s, uint32_t color,
                       uint32_t shadow) {
    txt(x + 1, y + 1, s, shadow);
    txt(x, y, s, color);
}

static void spaced_text(int cx, int y, const char *s, int spacing,
                        uint32_t color, uint32_t shadow) {
    int n = slen(s);
    int total = n * spacing;
    int x = cx - total / 2;
    char one[2];
    one[1] = '\0';
    for (int i = 0; i < n; i++) {
        one[0] = s[i];
        if (one[0] == ' ') { continue; }
        txt(x + i * spacing + 1, y + 1, one, shadow);
        txt(x + i * spacing - 1, y - 1, one, shadow);
        txt(x + i * spacing + 1, y - 1, one, shadow);
        txt(x + i * spacing - 1, y + 1, one, shadow);
        txt(x + i * spacing, y, one, color);
    }
}

static void draw_button(int x, int y, int w, int h, const char *label,
                        int enabled, int active) {
    uint32_t bg = active ? 0xFF2E3A4E : 0xFF20242F;
    if (!enabled) bg = 0xFF191C24;
    rect(x, y, w, h, bg);
    frame(x, y, w, h, active ? C_SEL : (enabled ? C_LINE : 0xFF262A33));
    txt_center(x + w / 2, y + (h - 8) / 2 - 1, label,
               enabled ? (active ? C_SEL : C_TEXT) : 0xFF5A6272);
}

/* ------------------------------------------------------------------ */
/* The map                                                             */
/* ------------------------------------------------------------------ */

static int map_focus_realm = -1;

static int map_dim;

static uint32_t prov_map_fill(int p) {
    uint32_t fill;
    if (map_mode == 1) {
        fill = mix_color(0xFF20241C, 0xFFE3C070, prov[p].dev * 26);
    } else {
        fill = prov_fill[p];
    }
    if (prov[p].occ) fill = mix_color(fill, ck3_realm_defs[prov[p].occ - 1].color, 120);
    if (map_focus_realm >= 0 && prov[p].owner == map_focus_realm)
        fill = mix_color(fill, 0xFFFBF1D2, 40);
    if (p == selected_prov && screen == SCREEN_GAME) fill = prov_sel[p];
    if (map_dim) fill = mix_color(fill, 0xFF101319, 155);
    return fill;
}

static void draw_map(int x, int y, int w, int h) {
    map_x0 = x; map_y0 = y; map_w0 = w; map_h0 = h;
    uint32_t sea = map_dim ? mix_color(C_SEA, 0xFF101319, 155) : C_SEA;
    uint32_t deep = map_dim ? mix_color(C_SEA_DEEP, 0xFF101319, 155) : C_SEA_DEEP;
    rect(x, y, w, h, sea);
    for (int yy = y + 13; yy < y + h; yy += 26) rect(x, yy, w, 1, deep);

    for (int r = 0; r < CK3_MAP_H; r++) {
        int yy0 = y + (r * h) / CK3_MAP_H;
        int yy1 = y + ((r + 1) * h) / CK3_MAP_H;
        if (yy1 <= yy0) continue;
        int c = 0;
        while (c < GRID_COLS) {
            uint8_t p = grid[r][c];
            int start = c;
            while (c < GRID_COLS && grid[r][c] == p) c++;
            if (p == SEAP) continue;

            int xa = x + (start * SAMPLE * w) / CK3_MAP_W;
            int xb = x + (c * SAMPLE * w) / CK3_MAP_W;
            if (xb <= xa) xb = xa + 1;
            rect(xa, yy0, xb - xa, yy1 - yy0, prov_map_fill(p));

            int sel = (screen == SCREEN_GAME && (int)p == selected_prov);
            int left = (start > 0) ? grid[r][start - 1] : SEAP;
            int right = (c < GRID_COLS) ? grid[r][c] : SEAP;

            /* Vertical edges: coast, realm border or a thin province line. */
            if (left == SEAP) {
                rect(xa, yy0, 2, yy1 - yy0, sel ? C_MAP_SEL : C_COAST);
            } else if (prov[left].owner != prov[p].owner) {
                rect(xa, yy0, 2, yy1 - yy0,
                     sel ? C_MAP_SEL : C_REALM_LINE);
            } else {
                rect(xa, yy0, 1, yy1 - yy0, sel ? C_MAP_SEL : prov_edge[p]);
            }
            if (right == SEAP) {
                rect(xb - 2, yy0, 2, yy1 - yy0, sel ? C_MAP_SEL : C_COAST);
            } else if (prov[right].owner != prov[p].owner) {
                rect(xb - 2, yy0, 2, yy1 - yy0,
                     sel ? C_MAP_SEL : C_REALM_LINE);
            } else if (!sel) {
                rect(xb - 1, yy0, 1, yy1 - yy0, prov_edge[p]);
            }

            /* Horizontal edges, one segment per stretch of different land
             * above, so borders follow the sampled province shape. */
            if (r > 0) {
                int k = start;
                while (k < c) {
                    uint8_t above = grid[r - 1][k];
                    if (above == p) { k++; continue; }
                    int k0 = k;
                    while (k < c && grid[r - 1][k] != p) k++;
                    int ax = x + (k0 * SAMPLE * w) / CK3_MAP_W;
                    int bx = x + (k * SAMPLE * w) / CK3_MAP_W;
                    if (bx <= ax) bx = ax + 1;
                    if (sel) {
                        rect(ax, yy0, bx - ax, 2, C_MAP_SEL);
                    } else if (above == SEAP) {
                        rect(ax, yy0, bx - ax, 2, C_COAST);
                    } else if (prov[above].owner != prov[p].owner) {
                        rect(ax, yy0, bx - ax, 2, C_REALM_LINE);
                    } else {
                        rect(ax, yy0, bx - ax, 1, prov_edge[p]);
                    }
                }
            }
        }
    }
}

struct sea_name {
    const char *name;
    int16_t lon;
    int16_t lat;
};

static const struct sea_name sea_names[] = {
    {"ATLANTIC OCEAN", -85, 430},
    {"NORTH SEA", 30, 570},
    {"BALTIC", 190, 583},
    {"MEDITERRANEAN", 130, 355},
    {"BLACK SEA", 340, 448},
    {"CASPIAN", 505, 418},
    {"RED SEA", 385, 225},
    {"ARABIAN SEA", 640, 175},
    {"BAY OF BENGAL", 880, 200},
    {"SOUTH CHINA SEA", 1090, 215},
    {"GULF OF GUINEA", -20, 130},
};

#define LBL_MAX 72

struct label_box {
    int x, y, w;
};

static int lbl_overlaps(const struct label_box *boxes, int n, int x, int y, int w) {
    for (int i = 0; i < n; i++) {
        if (x < boxes[i].x + boxes[i].w + 6 && boxes[i].x < x + w + 6 &&
            y < boxes[i].y + 16 && boxes[i].y < y + 16)
            return 1;
    }
    return 0;
}

static void draw_map_labels(int x, int y, int w, int h) {
    int i;
    struct label_box boxes[LBL_MAX];
    int nbox = 0;
    for (i = 0; i < (int)(sizeof(sea_names) / sizeof(sea_names[0])); i++) {
        int cx = x + ((int)sea_names[i].lon - CK3_LON_MIN) * w /
                      (CK3_LON_MAX - CK3_LON_MIN);
        int cy = y + (CK3_LAT_MAX - (int)sea_names[i].lat) * h /
                     (CK3_LAT_MAX - CK3_LAT_MIN);
        if (cx < 40 || cx > x + w - 40) continue;
        if (cy < y || cy > y + h - 16) continue;
        txt_center(cx, cy, sea_names[i].name, map_dim ? 0xFF4C4E48 : C_SEA_TEXT);
    }

    for (int r = 0; r < REALM_MAX; r++) {
        if (!realm[r].alive) continue;
        int n = slen(ck3_realm_defs[r].label);
        int cx = x + ((int)ck3_realm_defs[r].label_lon - CK3_LON_MIN) * w /
                      (CK3_LON_MAX - CK3_LON_MIN);
        int cy = y + (CK3_LAT_MAX - (int)ck3_realm_defs[r].label_lat) * h /
                     (CK3_LAT_MAX - CK3_LAT_MIN);
        if (cx < x + 24 || cx > x + w - 24) continue;
        int spacing = n > 12 ? 13 : (n > 8 ? 15 : 18);
        uint32_t ink = mix_color(ck3_realm_defs[r].color, C_INK, 140);
        if (r == player && screen == SCREEN_GAME) ink = 0xFF2A2118;
        int lw = slen(ck3_realm_defs[r].label) * spacing;
        if (cy < y || cy > y + h - 16) continue;
        if (lbl_overlaps(boxes, nbox, cx - lw / 2, cy, lw)) continue;
        spaced_text(cx, cy, ck3_realm_defs[r].label, spacing, ink, C_PAPER);
        if (nbox < LBL_MAX) {
            boxes[nbox].x = cx - lw / 2;
            boxes[nbox].y = cy;
            boxes[nbox].w = lw;
            nbox++;
        }
    }

    if (w < 640) return;
    for (int p = 0; p < PROV_MAX; p++) {
        int cx = x + (int)prov[p].seat_x * w / CK3_MAP_W;
        int cy = y + (int)prov[p].seat_y * h / CK3_MAP_H;
        if (p == selected_prov) continue;
        if (cy < y || cy > y + h - 12) continue;
        if (nbox < LBL_MAX && lbl_overlaps(boxes, nbox, cx + 2, cy - 6,
                                          slen(ck3_prov_defs[p].name) * 8)) continue;
        rect(cx - 1, cy - 1, 3, 3, C_INK);
        txt_shadow(cx + 4, cy - 5, ck3_prov_defs[p].name, 0xFF4A4A38, C_PAPER);
        if (nbox < LBL_MAX) {
            boxes[nbox].x = cx + 2;
            boxes[nbox].y = cy - 6;
            boxes[nbox].w = slen(ck3_prov_defs[p].name) * 8;
            nbox++;
        }
    }

    if (screen == SCREEN_GAME && selected_prov >= 0) {
        int cx = x + (int)prov[selected_prov].seat_x * w / CK3_MAP_W;
        int cy = y + (int)prov[selected_prov].seat_y * h / CK3_MAP_H;
        const char *name = ck3_prov_defs[selected_prov].name;
        int pw = slen(name) * 8 + 12;
        rect(cx - pw / 2 - 1, cy - 26, pw + 2, 16, 0xFF1A1D24);
        frame(cx - pw / 2, cy - 25, pw, 14, C_SEL);
        txt_center(cx, cy - 22, name, C_SEL);
    }
}

static void draw_army_markers(int x, int y, int w, int h) {
    for (int i = 0; i < ARMY_MAX; i++) {
        if (!armies[i].alive) continue;
        int px = x + (int)prov[armies[i].prov].seat_x * w / CK3_MAP_W;
        int py = y + (int)prov[armies[i].prov].seat_y * h / CK3_MAP_H;
        uint32_t c = ck3_realm_defs[armies[i].owner].color;
        rect(px - 9, py + 2, 2, 18, 0xFF2A2012);
        rect(px - 8, py + 2, 16, 11, 0xFF15181E);
        rect(px - 7, py + 3, 14, 9, c);
        if (i == selected_army) {
            frame(px - 9, py + 1, 18, 13, C_SEL);
        }
        if (armies[i].siege > 0) {
            int bar = armies[i].siege > 100 ? 100 : armies[i].siege;
            rect(px - 8, py + 14, 16, 3, 0xFF20242C);
            rect(px - 8, py + 14, 16 * bar / 100, 3, C_RED);
        }
    }
    if (realm[player].war && screen == SCREEN_GAME) {
        int wi = realm[player].war - 1;
        int p = wars[wi].target;
        int px = x + (int)prov[p].seat_x * w / CK3_MAP_W;
        int py = y + (int)prov[p].seat_y * h / CK3_MAP_H;
        rect(px - 2, py - 22, 2, 12, C_RED);
        rect(px, py - 22, 9, 7, C_RED);
    }
}

/* ------------------------------------------------------------------ */
/* Court UI                                                            */
/* ------------------------------------------------------------------ */

static const char *const terrain_names[6] = {
    "plains", "forest", "hills", "marsh", "desert", "mountains"
};

static void clip_copy(char *dst, const char *src, int chars) {
    int i = 0;
    if (chars < 1) chars = 1;
    while (src && src[i] && i < chars - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static const char *month_name(int m) {
    static const char *const names[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (m < 1) m = 1;
    if (m > 12) m = 12;
    return names[m - 1];
}

static void draw_header(const struct layout *L) {
    rect(L->x, L->y, L->w, L->head_h, 0xFF10131A);
    rect(L->x, L->y + L->head_h - 1, L->w, 1, C_LINE);
    txt(L->x + 10, L->y + 7, "CRUSADER KINGS III", C_GOLD);

    char date[48];
    scopy(date, "", 48);
    snum(date, month, 48);
    scopy(date, month_name(month), 48);
    sappend(date, " ", 48);
    snum(date, year, 48);
    txt_center(L->x + L->w / 2, L->y + 7, date, C_TEXT);

    if (ticker[0]) {
        txt(L->x + 10, L->y + 22, "WORLD", 0xFF7F8DA0);
        char world[52];
        clip_copy(world, ticker, 50);
        txt(L->x + 58, L->y + 22, world, C_DIM);
    }

    if (realm[player].war) {
        int wi = realm[player].war - 1;
        int enemy = wars[wi].attacker == player ? wars[wi].defender
                                                : wars[wi].attacker;
        char war[48];
        scopy(war, "AT WAR - ", 48);
        sappend(war, ck3_realm_defs[enemy].label, 48);
        txt_right(L->speed_x - 10, L->y + 7, war, C_RED);
    }

    for (int i = 0; i < 4; i++) {
        int x = L->speed_x + i * (L->speed_w + 2);
        const char *labels[4] = {"II", ">", ">>", ">>>"};
        draw_button(x, L->speed_y, L->speed_w, L->speed_h, labels[i], 1,
                    speed == i);
    }
    draw_button(L->next_x, L->next_y, L->next_w, L->next_h, "Next month",
                !game_over, 0);
}

static void draw_ruler_card(const struct layout *L) {
    int x = L->ruler_x, y = L->ruler_y;
    int w = L->ruler_w, h = L->ruler_h;
    uint32_t house_color = ck3_realm_defs[player].color;

    rect(x, y, w, h, C_UI2);
    frame(x, y, w, h, C_LINE);
    rect(x + 8, y + 8, 58, 58, 0xFF0E1117);
    frame(x + 8, y + 8, 58, 58, C_GOLD);
    rect(x + 11, y + 11, 52, 52, shade_color(house_color, 70));
    /* A simple crowned head: a shield, a circlet and the ruler's initial. */
    rect(x + 20, y + 34, 34, 26, house_color);
    rect(x + 25, y + 26, 24, 24, mix_color(house_color, 0xFFF1E4C6, 90));
    rect(x + 22, y + 21, 30, 5, C_GOLD);
    rect(x + 26, y + 17, 5, 6, C_GOLD);
    rect(x + 35, y + 15, 5, 8, C_GOLD);
    rect(x + 44, y + 17, 5, 6, C_GOLD);
    {
        char initial[2];
        initial[0] = ck3_realm_defs[player].ruler[0];
        initial[1] = '\0';
        txt_center(x + 37, y + 47, initial, 0xFF211826);
    }

    char line[40];
    clip_copy(line, ck3_realm_defs[player].ruler, 19);
    txt(x + 74, y + 6, line, C_TEXT);
    clip_copy(line, ck3_realm_defs[player].house, 19);
    sappend(line, " ", 40);
    snum(line, realm[player].age, 40);
    txt(x + 74, y + 22, line, C_DIM);

    rect(x + 74, y + 38, 150, 6, 0xFF10131A);
    rect(x + 74, y + 38, 150 * realm[player].health / 100, 6,
         realm[player].health > 55 ? C_GREEN : C_RED);
    txt(x + 74, y + 46, realm[player].heir ? "heir ready" : "no heir", C_DIM);
    {
        char v[16];
        scopy(v, "V ", 16);
        snum(v, realm[player].opinion, 16);
        txt_right(x + w - 8, y + 46, v,
                  realm[player].opinion < 35 ? C_RED : C_GREEN);
    }

    scopy(line, "G ", 40);
    snum(line, realm[player].gold, 40);
    sappend(line, "  P ", 40);
    snum(line, realm[player].prestige, 40);
    sappend(line, "  Pi ", 40);
    snum(line, realm[player].piety, 40);
    txt(x + 8, y + h - 22, line, C_GOLD);
    scopy(line, "Levies ", 40);
    snum(line, realm_levies(player), 40);
    sappend(line, "  Host ", 40);
    snum(line, realm_host(player), 40);
    txt(x + 8, y + h - 12, line, C_DIM);
}

static const char *const tab_label[5] = {
    "DECISIONS", "REALM", "ARMIES", "WAR", "CHRONICLE"
};
static const int tab_panel[5] = {
    PANEL_DECISIONS, PANEL_REALM, PANEL_ARMY, PANEL_WAR, PANEL_CHRONICLE
};

static int tab_x_at(const struct layout *L, int i) {
    return L->tab_x + i * (L->tab_w + 4);
}

static void draw_tabs(const struct layout *L) {
    for (int i = 0; i < 5; i++) {
        int hi = (panel == tab_panel[i]);
        if (i == 3 && realm[player].war && !hi) {
            rect(tab_x_at(L, i), L->tab_y, L->tab_w, L->tab_h, 0xFF3A2028);
            frame(tab_x_at(L, i), L->tab_y, L->tab_w, L->tab_h, 0xFF6A3038);
            txt_center(tab_x_at(L, i) + L->tab_w / 2, L->tab_y + 9,
                       tab_label[i], C_RED);
            continue;
        }
        draw_button(tab_x_at(L, i), L->tab_y, L->tab_w, L->tab_h,
                    tab_label[i], 1, hi);
    }
}

static void draw_chronicle_bar(const struct layout *L) {
    int x = tab_x_at(L, 4) + L->tab_w + 10;
    int y = L->ruler_y;
    int w = L->x + L->w - 8 - x;
    int h = L->ruler_h;
    if (w < 120) return;
    rect(x, y, w, h, C_UI2);
    frame(x, y, w, h, C_LINE);
    txt(x + 8, y + 5, "THE CHRONICLE", 0xFF7F8DA0);
    int lines = imin(4, chron_count);
    for (int i = 0; i < lines; i++) {
        int idx = chron_count - lines + i;
        char line[40];
        clip_copy(line, chron[idx], w / 8 - 2 < 39 ? w / 8 - 2 : 39);
        txt(x + 8, y + 22 + i * 15, line, i == lines - 1 ? C_TEXT : C_DIM);
    }
}

static void draw_panel_frame(const struct layout *L, const char *title) {
    rect(L->panel_x, L->panel_y, L->panel_w, L->panel_h, 0xFA141821);
    frame(L->panel_x, L->panel_y, L->panel_w, L->panel_h, C_LINE);
    rect(L->panel_x + 1, L->panel_y + 1, L->panel_w - 2, 26, C_UI3);
    txt(L->panel_x + 10, L->panel_y + 8, title, C_GOLD);
    rect(L->panel_x + 1, L->panel_y + 27, L->panel_w - 2, 1, C_LINE);
}

static int dec_row_y(const struct layout *L, int i) {
    return L->panel_y + 36 + i * 38;
}

static void draw_panel_decisions(const struct layout *L) {
    draw_panel_frame(L, "DECISIONS");
    for (int i = 0; i < DEC_COUNT; i++) {
        int y = dec_row_y(L, i);
        int en = dec_enabled(i);
        rect(L->panel_x + 8, y, L->panel_w - 16, 34,
             en ? 0xFF1D2330 : 0xFF171A21);
        frame(L->panel_x + 8, y, L->panel_w - 16, 34,
              en ? C_LINE : 0xFF23262E);
        rect(L->panel_x + 14, y + 9, 16, 16, en ? C_GOLD : 0xFF3A3F49);
        txt(L->panel_x + 38, y + 5, dec_title[i], en ? C_TEXT : 0xFF69707E);

        char cost[40];
        scopy(cost, "", 40);
        snum(cost, dec_cost[i], 40);
        sappend(cost, " gold", 40);
        if (i == 2) { sappend(cost, "  dev ", 40); snum(cost, prov[selected_prov].dev, 40); }
        if (i == 3) { sappend(cost, "  fort ", 40); snum(cost, prov[selected_prov].fort, 40); }
        if (i == 4) { sappend(cost, "  men ", 40); snum(cost, prov[selected_prov].levy, 40); }
        if (i == 5 && prov[selected_prov].claim) scopy(cost, "a claim is already held", 40);
        txt(L->panel_x + 38, y + 19, cost, en ? C_DIM : 0xFF545A66);
    }
}

static void draw_panel_realm(const struct layout *L) {
    char line[64];
    draw_panel_frame(L, "YOUR REALM");
    int y = L->panel_y + 34;
    txt(L->panel_x + 10, y, ck3_realm_defs[player].name, C_TEXT);
    y += 18;
    scopy(line, "Provinces ", 64);
    snum(line, realm_provinces(player), 64);
    sappend(line, "   Income ", 64);
    snum(line, realm_income(player), 64);
    sappend(line, "   Levies ", 64);
    snum(line, realm_levies(player), 64);
    txt(L->panel_x + 10, y, line, C_DIM);
    y += 16;
    scopy(line, "Hosts ", 64);
    snum(line, realm_armies(player), 64);
    sappend(line, "   Men ", 64);
    snum(line, realm_host(player), 64);
    sappend(line, "   Forts ", 64);
    {
        int forts = 0;
        for (int i = 0; i < PROV_MAX; i++)
            if (prov[i].owner == player) forts += prov[i].fort;
        snum(line, forts, 64);
    }
    txt(L->panel_x + 10, y, line, C_DIM);
    y += 16;
    scopy(line, "Vassals ", 64);
    snum(line, realm[player].opinion, 64);
    sappend(line, realm[player].opinion < 35 ? "  (restless)" : "  (content)", 64);
    txt(L->panel_x + 10, y, line,
        realm[player].opinion < 35 ? C_RED : C_GREEN);
    y += 20;
    rect(L->panel_x + 8, y, L->panel_w - 16, 1, C_LINE);
    y += 8;
    txt(L->panel_x + 10, y, "PROVINCE            DEV  LEVY  FORT", 0xFF7F8DA0);
    y += 16;
    for (int i = 0; i < PROV_MAX && y < L->panel_y + L->panel_h - 18; i++) {
        if (prov[i].owner != player) continue;
        char name[20];
        clip_copy(name, ck3_prov_defs[i].name, 18);
        while (slen(name) < 19) sappend(name, " ", 20);
        scopy(line, name, 64);
        snum(line, prov[i].dev, 64);
        sappend(line, "     ", 64);
        snum(line, prov[i].levy, 64);
        sappend(line, "    ", 64);
        snum(line, prov[i].fort, 64);
        txt(L->panel_x + 10, y, line, prov[i].occ ? C_RED : C_TEXT);
        y += 15;
    }
}

static int army_row_y(const struct layout *L, int i) {
    return L->panel_y + 56 + i * 38;
}

static void draw_panel_army(const struct layout *L) {
    char line[64];
    draw_panel_frame(L, "YOUR HOSTS");
    int shown = 0;
    for (int i = 0; i < ARMY_MAX && shown < 5; i++) {
        if (!armies[i].alive || armies[i].owner != player) continue;
        int y = army_row_y(L, shown);
        rect(L->panel_x + 8, y, L->panel_w - 16, 34,
             i == selected_army ? 0xFF2A3550 : 0xFF1B202B);
        frame(L->panel_x + 8, y, L->panel_w - 16, 34,
              i == selected_army ? C_SEL : C_LINE);
        scopy(line, "", 64);
        snum(line, armies[i].men, 64);
        sappend(line, " men  ", 64);
        sappend(line, ck3_prov_defs[armies[i].prov].name, 64);
        txt(L->panel_x + 16, y + 5, line, C_TEXT);
        scopy(line, "morale ", 64);
        snum(line, armies[i].morale, 64);
        if (armies[i].target != 255) {
            sappend(line, "  marching to ", 64);
            sappend(line, ck3_prov_defs[armies[i].target].name, 64);
        } else if (armies[i].siege > 0) {
            sappend(line, "  siege ", 64);
            snum(line, armies[i].siege, 64);
            sappend(line, "%", 64);
        } else {
            sappend(line, "  holding", 64);
        }
        txt(L->panel_x + 16, y + 19, line, C_DIM);
        shown++;
    }
    if (shown == 0) {
        txt(L->panel_x + 14, L->panel_y + 44, "No hosts are raised.", C_DIM);
        txt(L->panel_x + 14, L->panel_y + 62, "Raise levies in a province you", C_DIM);
        txt(L->panel_x + 14, L->panel_y + 77, "hold, then click a province", C_DIM);
        txt(L->panel_x + 14, L->panel_y + 92, "to order a march.", C_DIM);
    } else {
        txt(L->panel_x + 14, L->panel_y + L->panel_h - 78,
            "Click a host to choose it, then", C_DIM);
        txt(L->panel_x + 14, L->panel_y + L->panel_h - 63,
            "click a province to march there.", C_DIM);
    }
    int by = L->panel_y + L->panel_h - 36;
    draw_button(L->panel_x + 8, by, (L->panel_w - 22) / 2, 26, "Raise levies",
                dec_enabled(4), 0);
    draw_button(L->panel_x + 14 + (L->panel_w - 22) / 2, by,
                (L->panel_w - 22) / 2, 26, "Disband",
                army_slot_selected() >= 0, 0);
}

static void draw_panel_war(const struct layout *L) {
    char line[64];
    draw_panel_frame(L, "THE WAR");
    if (!realm[player].war) {
        txt(L->panel_x + 14, L->panel_y + 44, "Your realm is at peace.", C_DIM);
        txt(L->panel_x + 14, L->panel_y + 64, "Right-click a foreign province", C_DIM);
        txt(L->panel_x + 14, L->panel_y + 79, "and choose Declare war.", C_DIM);
        return;
    }
    int wi = realm[player].war - 1;
    struct war_state *w = &wars[wi];
    int enemy = w->attacker == player ? w->defender : w->attacker;
    int side = w->attacker == player ? 1 : -1;
    int mine = w->score * side;

    txt(L->panel_x + 14, L->panel_y + 38, ck3_realm_defs[enemy].name, C_RED);
    scopy(line, casus_label[w->casus], 64);
    sappend(line, " for ", 64);
    sappend(line, ck3_prov_defs[w->target].name, 64);
    txt(L->panel_x + 14, L->panel_y + 56, line, C_DIM);

    scopy(line, "War score ", 64);
    snum(line, mine, 64);
    sappend(line, " of 100", 64);
    txt(L->panel_x + 14, L->panel_y + 80, line, C_TEXT);
    int bar = (mine + 100) * 150 / 200;
    if (bar < 0) bar = 0;
    if (bar > 150) bar = 150;
    rect(L->panel_x + 14, L->panel_y + 98, 150, 8, 0xFF10131A);
    rect(L->panel_x + 14 + 75, L->panel_y + 98, 1, 8, C_DIM);
    if (mine >= 0) rect(L->panel_x + 14 + 75, L->panel_y + 98, bar - 75, 8, C_GREEN);
    else rect(L->panel_x + 14 + bar, L->panel_y + 98, 75 - bar, 8, C_RED);

    txt(L->panel_x + 14, L->panel_y + 116,
        mine >= 40 ? "You are winning the war." :
        mine <= -40 ? "You are losing the war." : "The war is evenly matched.", C_DIM);

    int y = L->panel_y + 140;
    txt(L->panel_x + 14, y, "OCCUPIED PROVINCES", 0xFF7F8DA0);
    y += 17;
    int shown = 0;
    for (int i = 0; i < PROV_MAX && shown < 4; i++) {
        if (!prov[i].occ) continue;
        int occ = prov[i].occ - 1;
        if (occ != player && occ != enemy) continue;
        scopy(line, ck3_prov_defs[i].name, 64);
        if (occ == player) sappend(line, "  (yours)", 64);
        else sappend(line, "  (theirs)", 64);
        txt(L->panel_x + 14, y, line, occ == player ? C_GREEN : C_RED);
        y += 15;
        shown++;
    }
    if (!shown) txt(L->panel_x + 14, y, "none", C_DIM);

    int by = L->panel_y + L->panel_h - 36;
    draw_button(L->panel_x + 8, by, L->panel_w - 16, 26, "Negotiate peace",
                w->months > 1, 0);
}

static void draw_panel_chronicle(const struct layout *L) {
    draw_panel_frame(L, "THE CHRONICLE");
    int y = L->panel_y + 36;
    int first = chron_count > 13 ? chron_count - 13 : 0;
    for (int i = first; i < chron_count; i++) {
        char line[40];
        clip_copy(line, chron[i], L->panel_w / 8 - 3);
        txt(L->panel_x + 10, y, line, i == chron_count - 1 ? C_TEXT : C_DIM);
        y += 16;
    }
}

static void draw_panel(const struct layout *L) {
    switch (panel) {
        case PANEL_DECISIONS: draw_panel_decisions(L); break;
        case PANEL_REALM: draw_panel_realm(L); break;
        case PANEL_ARMY: draw_panel_army(L); break;
        case PANEL_WAR: draw_panel_war(L); break;
        case PANEL_CHRONICLE: draw_panel_chronicle(L); break;
        default: break;
    }
}

static void draw_province_box(const struct layout *L) {
    int x = L->prov_x, y = L->prov_y, w = L->prov_w, h = L->prov_h;
    char line[64];
    rect(x, y, w, h, 0xFA141821);
    frame(x, y, w, h, C_LINE);
    rect(x + 1, y + 1, w - 2, 24, C_UI3);
    txt(x + 9, y + 6, ck3_prov_defs[selected_prov].name, C_GOLD);

    int owner = prov[selected_prov].owner;
    rect(x + 9, y + 34, 10, 10, ck3_realm_defs[owner].color);
    txt(x + 25, y + 33, ck3_realm_defs[owner].name, C_TEXT);

    scopy(line, terrain_names[prov[selected_prov].terrain], 64);
    sappend(line, "   dev ", 64);
    snum(line, prov[selected_prov].dev, 64);
    sappend(line, "   fort ", 64);
    snum(line, prov[selected_prov].fort, 64);
    txt(x + 9, y + 54, line, C_DIM);

    scopy(line, "levies ", 64);
    snum(line, prov[selected_prov].levy, 64);
    sappend(line, "   tax ", 64);
    snum(line, prov_income(selected_prov), 64);
    txt(x + 9, y + 70, line, C_DIM);

    if (prov[selected_prov].occ) {
        scopy(line, "OCCUPIED by ", 64);
        sappend(line, ck3_realm_defs[prov[selected_prov].occ - 1].label, 64);
        txt(x + 9, y + 88, line, C_RED);
    } else if (prov[selected_prov].claim) {
        txt(x + 9, y + 88, "You hold a claim here", C_GREEN);
    } else if (owner == player) {
        txt(x + 9, y + 88, "Your demesne", C_GREEN);
    } else {
        txt(x + 9, y + 88, "Right-click for actions", C_DIM);
    }

    if (owner == player && army_slot_selected() >= 0) {
        txt(x + 9, y + 108, "Click to march the host here", 0xFF7F8DA0);
    } else if (owner != player) {
        txt(x + 9, y + 108, "Right-click: war, claims", 0xFF7F8DA0);
    }
    txt(x + 9, y + h - 16, "WASD browse  E month  Tab tabs", 0xFF5C6576);
}

/* ------------------------------------------------------------------ */
/* Modals and the context menu                                         */
/* ------------------------------------------------------------------ */

static const char *faith_name(int f) {
    switch (f) {
        case F_CHRISTIAN: return "Christian";
        case F_SUNNI: return "Sunni";
        case F_SHIA: return "Shia";
        case F_PAGAN: return "Pagan";
        case F_HINDU: return "Hindu";
        case F_BUDDHIST: return "Buddhist";
        default: return "Unknown";
    }
}

static void modal_box(const struct layout *L, int bw, int bh, int *bx, int *by) {
    if (bw > L->w - 24) bw = L->w - 24;
    if (bh > L->h - 24) bh = L->h - 24;
    *bx = L->x + (L->w - bw) / 2;
    *by = L->y + (L->h - bh) / 2;
}

static int event_option_y(int by, int i) { return by + 116 + i * 32; }

static void draw_modal_event(const struct layout *L) {
    int bx, by;
    char title[48], text[64], opt[64];
    modal_box(L, 480, 232, &bx, &by);
    rect(bx, by, 480, 232, 0xF50E1118);
    frame(bx, by, 480, 232, C_SEL);
    rect(bx + 1, by + 1, 478, 30, C_UI3);

    if (event_kind >= 100) {
        int wi = modal_arg;
        int enemy = wars[wi].attacker == player ? wars[wi].defender : wars[wi].attacker;
        scopy(title, event_kind == 100 ? "Peace offered" : "The enemy sues for peace", 48);
        if (event_kind == 100) {
            scopy(text, ck3_realm_defs[enemy].label, 64);
            sappend(text, " offers peace. The terms would cost you the war goal.", 64);
        } else {
            scopy(text, ck3_realm_defs[enemy].label, 64);
            sappend(text, " has had enough, and offers peace.", 64);
        }
        txt(bx + 12, by + 9, title, C_SEL);
        txt(bx + 12, by + 46, text, C_TEXT);
        txt(bx + 12, by + 66, "Choose how the war ends.", C_DIM);
        txt(bx + 20, event_option_y(by, 0) + 2, "[1] Accept the peace", C_GREEN);
        txt(bx + 20, event_option_y(by, 1) + 2, "[2] Fight on", C_RED);
        txt(bx + 12, by + 212, "Esc closes the offer and the war goes on.", 0xFF5C6576);
        return;
    }

    const struct event_def *e = &events[event_kind];
    scopy(title, e->title, 48);
    txt(bx + 12, by + 9, title, C_SEL);
    txt(bx + 12, by + 44, e->text, C_TEXT);
    txt(bx + 12, by + 64, "Your court awaits your word.", C_DIM);
    txt(bx + 20, event_option_y(by, 0) + 2, "[1]", C_GREEN);
    clip_copy(opt, e->opt1, 52);
    txt(bx + 44, event_option_y(by, 0) + 2, opt, C_TEXT);
    txt(bx + 20, event_option_y(by, 1) + 2, "[2]", C_RED);
    clip_copy(opt, e->opt2, 52);
    txt(bx + 44, event_option_y(by, 1) + 2, opt, C_TEXT);
    txt(bx + 12, by + 212, "Choose one to continue the chronicle.", 0xFF5C6576);
}

static void draw_modal_war_decl(const struct layout *L) {
    int bx, by;
    char line[64];
    int defending = prov[wdecl_prov].owner;
    modal_box(L, 600, 316, &bx, &by);
    rect(bx, by, 600, 316, 0xF50E1118);
    frame(bx, by, 600, 316, C_RED);
    rect(bx + 1, by + 1, 598, 30, 0xFF2A1C22);
    txt(bx + 12, by + 9, "DECLARE WAR", C_RED);

    scopy(line, "Target: ", 64);
    sappend(line, ck3_realm_defs[defending].name, 64);
    txt(bx + 12, by + 38, line, C_TEXT);
    scopy(line, "for the province of ", 64);
    sappend(line, ck3_prov_defs[wdecl_prov].name, 64);
    txt(bx + 12, by + 54, line, C_DIM);
    if (truce[player][defending] > (uint16_t)abs_month()) {
        txt(bx + 12, by + 72, "A truce still holds; no cause can be pressed.",
            C_RED);
    } else {
        txt(bx + 12, by + 72, "Why do you go to war?", 0xFF9AA6B8);
    }

    for (int i = 0; i < CB_COUNT; i++) {
        int y = by + 92 + i * 46;
        int ok = casus_available(i, wdecl_prov);
        rect(bx + 12, y, 576, 42, ok ? 0xFF1D2330 : 0xFF171A21);
        frame(bx + 12, y, 576, 42, ok ? C_LINE : 0xFF23262E);
        txt(bx + 22, y + 5, "[", ok ? C_GOLD : 0xFF555C68);
        char idx[4];
        idx[0] = '\0';
        snum(idx, i + 1, 4);
        txt(bx + 30, y + 5, idx, ok ? C_GOLD : 0xFF555C68);
        txt(bx + 38, y + 5, "]", ok ? C_GOLD : 0xFF555C68);
        txt(bx + 54, y + 5, casus_label[i], ok ? C_TEXT : 0xFF69707E);
        txt(bx + 54, y + 21, casus_why[i], ok ? C_DIM : 0xFF4E5560);
    }
    txt(bx + 12, by + 290, "1-4 choose a cause   Esc cancels   war costs prestige or piety", 0xFF5C6576);
}

static int peace_option_y(int by, int i) { return by + 92 + i * 34; }

static const char *const peace_label[5] = {
    "White peace", "Demand the war goal", "Demand tribute (80 gold)",
    "Offer tribute (80 gold)", "Surrender"
};

static void draw_modal_peace(const struct layout *L) {
    int bx, by;
    char line[64];
    int wi = realm[player].war - 1;
    int side = wars[wi].attacker == player ? 1 : -1;
    int mine = wars[wi].score * side;
    modal_box(L, 520, 288, &bx, &by);
    rect(bx, by, 520, 288, 0xF50E1118);
    frame(bx, by, 520, 288, C_SEL);
    rect(bx + 1, by + 1, 518, 30, C_UI3);
    txt(bx + 12, by + 9, "PEACE NEGOTIATIONS", C_SEL);
    scopy(line, "War score: ", 64);
    snum(line, mine, 64);
    sappend(line, ". Better terms unlock at 30 and 55.", 64);
    txt(bx + 12, by + 40, line, C_DIM);

    for (int i = 0; i < 5; i++) {
        int y = peace_option_y(by, i);
        int ok = peace_available(i);
        rect(bx + 12, y, 496, 30, ok ? 0xFF1D2330 : 0xFF171A21);
        frame(bx + 12, y, 496, 30, ok ? C_LINE : 0xFF23262E);
        txt(bx + 22, y + 8, "[", ok ? C_GOLD : 0xFF555C68);
        char idx[4];
        idx[0] = '\0';
        snum(idx, i + 1, 4);
        txt(bx + 30, y + 8, idx, ok ? C_GOLD : 0xFF555C68);
        txt(bx + 38, y + 8, "]", ok ? C_GOLD : 0xFF555C68);
        txt(bx + 52, y + 8, peace_label[i], ok ? C_TEXT : 0xFF69707E);
    }
    txt(bx + 12, by + 268, "1-5 choose   Esc returns to the war", 0xFF5C6576);
}

static void draw_context(const struct layout *L) {
    if (!ctx_open) return;
    int w = 236;
    int h = ctx_count * 24 + 8;
    int x = ctx_x, y = ctx_y;
    if (x + w > L->x + L->w - 4) x = L->x + L->w - w - 4;
    if (y + h > L->y + L->h - 4) y = L->y + L->h - h - 4;
    if (x < L->x + 4) x = L->x + 4;
    if (y < L->y + 4) y = L->y + 4;
    rect(x, y, w, h, 0xFF161A23);
    frame(x, y, w, h, C_SEL);
    for (int i = 0; i < ctx_count; i++) {
        int ry = y + 4 + i * 24;
        if (i == 0) txt(x + 8, ry + 7, ctx_label[i],
                        ctx_enabled[i] ? C_GOLD : 0xFF5A6170);
        else txt(x + 8, ry + 7, ctx_label[i],
                 ctx_enabled[i] ? C_TEXT : 0xFF5A6170);
    }
}

/* ------------------------------------------------------------------ */
/* Screens                                                             */
/* ------------------------------------------------------------------ */

static void draw_game_over(const struct layout *L) {
    int bx, by, bw = 420, bh = 130;
    modal_box(L, bw, bh, &bx, &by);
    char line[48];
    rect(bx, by, bw, bh, 0xF00E1018);
    frame(bx, by, bw, bh, C_RED);
    txt(bx + 20, by + 16, "THE DYNASTY ENDS", C_RED);
    txt(bx + 20, by + 42, "No heir survives to hold your seat.", C_TEXT);
    scopy(line, "Final prestige: ", 48);
    snum(line, realm[player].prestige, 48);
    txt(bx + 20, by + 66, line, C_DIM);
    txt(bx + 20, by + 96, "R begins a new chronicle.", C_SEL);
}

static void draw_menu_plate(int x, int y, int cx, int pw, int ph) {
    rect(x + 4, y + 5, pw, ph, 0x66070A10);
    rect(x, y, pw, ph, 0xF20D1119);
    frame(x, y, pw, ph, C_GOLD);
    rect(x + 1, y + 1, pw - 2, 54, 0xF0161B26);
    rect(x + 1, y + 55, pw - 2, 1, C_LINE);
    rect(cx - 30, y + 28, 60, 5, C_GOLD);
    rect(cx - 30, y + 14, 6, 16, C_GOLD);
    rect(cx - 3, y + 8, 6, 22, C_GOLD);
    rect(cx + 24, y + 14, 6, 16, C_GOLD);
    rect(cx - 22, y + 20, 8, 4, C_GOLD);
    rect(cx + 14, y + 20, 8, 4, C_GOLD);
    txt_center(cx, y + 38, "CRUSADER KINGS III", C_GOLD);
}

static void draw_title(int x, int y, int w, int h) {
    static const char *const items[3] = {
        "New chronicle", "Continue chronicle", "How to play"
    };
    map_focus_realm = -1;
    map_dim = 1;
    draw_map(x, y, w, h);
    draw_map_labels(x, y, w, h);
    map_dim = 0;

    int pw = 460, ph = 300;
    int px = x + (w - pw) / 2;
    int py = y + (h - ph) / 2;
    draw_menu_plate(px, py, px + pw / 2, pw, ph);

    int iy = py + 84;
    for (int i = 0; i < 3; i++) {
        int disabled = (i == 1 && !started && !save_ok);
        uint32_t color = disabled ? 0xFF4A5262
                                  : (i == menu_sel ? C_SEL : C_TEXT);
        if (i == menu_sel && !disabled) {
            rect(px + 60, iy - 4, pw - 120, 22, 0xFF1D2536);
            frame(px + 60, iy - 4, pw - 120, 22, 0xFF3D5A8C);
        }
        txt_center(px + pw / 2, iy, items[i], color);
        iy += 32;
    }

    txt_center(px + pw / 2, py + ph - 58, "1066 - thirty-seven realms await", C_DIM);
    txt_center(px + pw / 2, py + ph - 40, "W/S choose   Enter select", C_DIM);
    txt_center(px + pw / 2, py + ph - 22, "A Solis chronicle", 0xFF5C6576);
}

static int pick_panel_x(int x, int w, int pw) { return x + w - pw - 6; }

static int pick_rows_visible(int ph) {
    int n = (ph - 116) / 24;
    if (n > 13) n = 13;
    if (n < 3) n = 3;
    return n;
}

static void draw_pick(int x, int y, int w, int h) {
    int pw = w > 760 ? 384 : 320;
    int px = pick_panel_x(x, w, pw);
    int py = y + 6;
    int ph = h - 12;
    char line[64];

    map_focus_realm = pick_sel;
    selected_prov = -1;
    map_dim = 0;
    draw_map(x, y, w, h);
    draw_map_labels(x, y, w, h);

    rect(px, py, pw, ph, 0xF2141821);
    frame(px, py, pw, ph, C_GOLD);
    rect(px + 1, py + 1, pw - 2, 34, C_UI3);
    txt(px + 10, py + 6, "CHOOSE YOUR REALM, 1066", C_GOLD);
    txt(px + 10, py + 20, "Click a realm on the map or in this list", C_DIM);

    int rows = pick_rows_visible(ph);
    int top = py + 44;
    for (int i = 0; i < rows; i++) {
        int r = pick_scroll + i;
        if (r >= REALM_MAX) break;
        int ry = top + i * 24;
        int sel = (r == pick_sel);
        if (sel) {
            rect(px + 6, ry, pw - 12, 22, 0xFF2A3550);
            frame(px + 6, ry, pw - 12, 22, C_SEL);
        }
        rect(px + 12, ry + 6, 10, 10, ck3_realm_defs[r].color);
        txt(px + 28, ry + 4, ck3_realm_defs[r].label, sel ? C_SEL : C_TEXT);
        scopy(line, ck3_realm_defs[r].ruler, 64);
        txt(px + pw - 10 - slen(line) * 8, ry + 4, line, C_DIM);
    }

    int sy = top + rows * 24 + 8;
    rect(px + 8, sy, pw - 16, 1, C_LINE);
    sy += 8;
    txt(px + 10, sy, ck3_realm_defs[pick_sel].name, C_TEXT);
    sy += 17;
    scopy(line, ck3_realm_defs[pick_sel].ruler, 64);
    sappend(line, " of the house of ", 64);
    sappend(line, ck3_realm_defs[pick_sel].house, 64);
    txt(px + 10, sy, line, C_DIM);
    sy += 17;
    scopy(line, "Provinces ", 64);
    snum(line, realm_provinces(pick_sel), 64);
    sappend(line, "   Levies ", 64);
    snum(line, realm_levies(pick_sel), 64);
    sappend(line, "   Gold ", 64);
    snum(line, realm[pick_sel].gold, 64);
    txt(px + 10, sy, line, C_DIM);
    sy += 17;
    scopy(line, "Faith: ", 64);
    sappend(line, faith_name(ck3_realm_defs[pick_sel].faith), 64);
    txt(px + 10, sy, line, C_DIM);

    int by = py + ph - 28;
    draw_button(px + 8, by, (pw - 20) / 2, 22, "Back", 1, 0);
    draw_button(px + 12 + (pw - 20) / 2, by, (pw - 20) / 2, 22,
                "Begin the reign", 1, 1);
}

static void draw_help(int x, int y, int w, int h) {
    static const char *const lines[] = {
        "The map is Europe, Africa and Asia in 1066. Left-click a",
        "province to inspect it. Right-click it for its own menu:",
        "declare war, forge a claim, raise levies, march a host.",
        "",
        "W A S D     browse the map from province to province",
        "E / Space   advance one month",
        "Tab         cycle decisions, realm, armies, war, chronicle",
        "1 - 5       open a court tab directly",
        "J           declare war on the selected province",
        "K           develop the selected province (60 gold)",
        "F           hold a feast (30 gold)",
        "G           raise the levies of the selected province",
        "C           forge a claim (40 gold)",
        "M           switch between the political and dev map",
        "P           pause or resume the flow of months",
        "T           march the chosen host to the selected province",
        "H           choose the next host you have raised",
        "Y           skip a year",
        "V           save now (the game also autosaves each January)",
        "O           diagnostics: seed, repaint time, build",
        "Z           dump the state to the serial log",
        "Esc         close a panel, or return to the title screen",
        "",
        "Choose any realm on the pick screen. Taxes pay for wars,",
        "hosts take provinces, and every neighbour is watching."
    };
    map_focus_realm = -1;
    map_dim = 1;
    draw_map(x, y, w, h);
    map_dim = 0;
    rect(x, y, w, h, 0xE6080A10);
    int px = x + 40;
    int py = y + 30;
    txt(px, py, "HOW TO PLAY", C_GOLD);
    py += 28;
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    for (int i = 0; i < n; i++) {
        txt(px, py, lines[i], (i < 3) ? C_TEXT : C_DIM);
        py += 16;
    }
    txt(px, py + 4, "build", 0xFF5C6576);
    txt(px + 48, py + 4, sys_get_build_id(), 0xFF5C6576);
    txt(px, py + 22, "Press any key to return.", C_SEL);
}

/* Toggled with O. Answers the questions a screenshot cannot: which build is
 * this, which seed, how long does a full repaint take. */
static void draw_debug_overlay(const struct layout *L) {
    if (!debug_overlay) return;
    int x = L->map_x + 8;
    int y = L->map_y + 8;
    int w = 258, h = 88;
    char line[64];
    rect(x, y, w, h, 0xEE0C1016);
    frame(x, y, w, h, 0xFF4A90E2);
    txt(x + 8, y + 6, "DIAGNOSTICS", 0xFF4A90E2);
    int row = y + 22;
    scopy(line, "seed ", 64);
    snum(line, (int)(campaign_seed & 0x7FFFFFFFu), 64);
    txt(x + 8, row, line, C_DIM); row += 13;
    scopy(line, "month ", 64); snum(line, abs_month(), 64);
    sappend(line, "   repaint ", 64); snum(line, repaint_ms, 64);
    sappend(line, " ms", 64);
    txt(x + 8, row, line, C_DIM); row += 13;
    scopy(line, "hosts ", 64); snum(line, realm_armies(player), 64);
    sappend(line, "   men ", 64); snum(line, realm_host(player), 64);
    txt(x + 8, row, line, C_DIM); row += 13;
    scopy(line, "provinces ", 64); snum(line, realm_provinces(player), 64);
    sappend(line, "   vassals ", 64); snum(line, realm[player].opinion, 64);
    txt(x + 8, row, line, C_DIM); row += 13;
    scopy(line, "build ", 64);
    sappend(line, sys_get_build_id(), 64);
    txt(x + 8, row, line, 0xFF6A7482);
}

static void draw_game(int x, int y, int w, int h) {
    struct layout L;
    compute_layout(x, y, w, h, &L);
    rect(x, y, w, h, C_UI);
    map_dim = 0;
    if (selected_prov < 0 || selected_prov >= PROV_MAX)
        selected_prov = ck3_realm_defs[player].capital;

    draw_map(L.map_x, L.map_y, L.map_w, L.map_h);
    draw_army_markers(L.map_x, L.map_y, L.map_w, L.map_h);
    draw_map_labels(L.map_x, L.map_y, L.map_w, L.map_h);

    draw_header(&L);
    draw_ruler_card(&L);
    draw_tabs(&L);
    draw_chronicle_bar(&L);
    draw_province_box(&L);
    draw_panel(&L);
    draw_debug_overlay(&L);

    if (modal == MODAL_EVENT) draw_modal_event(&L);
    else if (modal == MODAL_WAR_DECL) draw_modal_war_decl(&L);
    else if (modal == MODAL_PEACE) draw_modal_peace(&L);
    else if (game_over) draw_game_over(&L);

    draw_context(&L);
}

/* ------------------------------------------------------------------ */
/* Starting a game                                                     */
/* ------------------------------------------------------------------ */

static void start_game(int r) {
    /* Seed from the wall clock and the tick counter so two campaigns differ,
     * then keep the seed: the diagnostics overlay, the save file and the
     * serial dump all report it, so an outcome can be reproduced instead of
     * described. */
    struct rtc_time now;
    rtc_get_time(&now);
    campaign_seed = (uint32_t)(now.year * 10000 + now.month * 100 + now.day) *
                    2654435761u;
    campaign_seed ^= (uint32_t)(now.hour * 3600 + now.minute * 60 + now.second);
    campaign_seed ^= timer_get_ticks() << 13;
    if (campaign_seed == 0) campaign_seed = 0x1066u;
    rng_state = campaign_seed ^ (uint32_t)(0x1066u + (uint32_t)r * 7919u);
    if (rng_state == 0) rng_state = 0x5EED1066u;
    player = r;
    province_defs_init();
    for (int i = 0; i < PROV_MAX; i++) prov[i].levy = (uint16_t)prov_levy_max(i);
    selected_prov = ck3_realm_defs[r].capital;
    selected_army = -1;
    panel = PANEL_NONE;
    map_focus_realm = r;
    modal = MODAL_NONE;
    ctx_open = 0;
    game_over = 0;
    year = 1066;
    month = 1;
    speed = 1;
    last_advance = timer_get_ticks();
    chron_count = 0;
    char line[64];
    scopy(line, ck3_realm_defs[r].ruler, 64);
    sappend(line, " takes the seat of ", 64);
    sappend(line, ck3_realm_defs[r].label, 64);
    sappend(line, ".", 64);
    chron_add(line);
    chron_add("The year is 1066. Neighbours watch the new reign.");
    started = 1;
    screen = SCREEN_GAME;
}

static void start_new_chronicle(void) {
    year = 1066;
    month = 1;
    screen = SCREEN_PICK;
    pick_sel = 0;
    pick_scroll = 0;
    realm[pick_sel].alive = 1;
    province_defs_init();
}

/* ------------------------------------------------------------------ */
/* Province selection                                                  */
/* ------------------------------------------------------------------ */

static int province_at(int mx, int my) {
    if (map_w0 <= 0 || map_h0 <= 0) return -1;
    if (mx < map_x0 || my < map_y0 || mx >= map_x0 + map_w0 || my >= map_y0 + map_h0)
        return -1;
    int px = (mx - map_x0) * CK3_MAP_W / map_w0;
    int py = (my - map_y0) * CK3_MAP_H / map_h0;
    if (px < 0 || px >= CK3_MAP_W || py < 0 || py >= CK3_MAP_H) return -1;
    int c = px / SAMPLE;
    if (c < 0 || c >= GRID_COLS) return -1;
    int p = grid[py][c];
    return p == SEAP ? -1 : p;
}

static void select_province(int p) {
    if (p < 0 || p >= PROV_MAX) return;
    selected_prov = p;
    if (prov[p].owner == player) {
        int a = army_at(player, p);
        if (a >= 0) selected_army = a;
    }
}

static void move_selection(int dx, int dy) {
    int best = -1;
    long best_score = 0;
    for (int i = 0; i < PROV_MAX; i++) {
        if (i == selected_prov) continue;
        int ddx = (int)prov[i].seat_x - (int)prov[selected_prov].seat_x;
        int ddy = (int)prov[i].seat_y - (int)prov[selected_prov].seat_y;
        if (dx > 0 && ddx <= 2) continue;
        if (dx < 0 && ddx >= -2) continue;
        if (dy > 0 && ddy <= 2) continue;
        if (dy < 0 && ddy >= -2) continue;
        long along = dx != 0 ? (long)iabs(ddx) : (long)iabs(ddy);
        long across = dx != 0 ? (long)ddy : (long)ddx;
        if (along < across) continue;
        long score = along * 4 + across * across * 4;
        if (best < 0 || score < best_score) { best = i; best_score = score; }
    }
    if (best >= 0) select_province(best);
}

/* ------------------------------------------------------------------ */
/* War declarations and peace                                          */
/* ------------------------------------------------------------------ */

static int casus_available(int cb, int p) {
    if (p < 0 || p >= PROV_MAX) return 0;
    if (prov[p].owner == player) return 0;
    if (realm[player].war) return 0;
    if (truce[player][prov[p].owner] > (uint16_t)abs_month()) return 0;
    switch (cb) {
        case CB_CLAIM:
            return prov[p].claim && realm[player].prestige >= 25;
        case CB_HOLY:
            return ck3_realm_defs[prov[p].owner].faith !=
                       ck3_realm_defs[player].faith && realm[player].piety >= 75;
        case CB_CONQUEST:
            return realm[player].prestige >= 50;
        default:
            return 1;
    }
}

static void open_war_decl(int p) {
    if (p < 0 || p >= PROV_MAX) return;
    if (prov[p].owner == player) { chron_add("That province is yours."); return; }
    if (realm[player].war) { chron_add("You are already at war. Make peace first."); return; }
    if (!realm[prov[p].owner].alive) return;
    wdecl_prov = p;
    modal = MODAL_WAR_DECL;
    speed = 0;
}

static void declare_player_war(int cb) {
    int p = wdecl_prov;
    if (!casus_available(cb, p)) return;
    int defender = prov[p].owner;
    if (cb == CB_CLAIM) {
        realm[player].prestige = (int16_t)(realm[player].prestige - 25);
        prov[p].claim = 0;
    } else if (cb == CB_HOLY) {
        realm[player].piety = (int16_t)(realm[player].piety - 75);
    } else if (cb == CB_CONQUEST) {
        realm[player].prestige = (int16_t)(realm[player].prestige - 50);
    }
    if (declare_war(player, defender, p, cb) < 0) {
        chron_add("The war cannot be declared now.");
        return;
    }
    modal = MODAL_NONE;
    panel = PANEL_WAR;
    selected_prov = p;
}

static int peace_available(int opt) {
    if (!realm[player].war) return 0;
    int wi = realm[player].war - 1;
    int side = wars[wi].attacker == player ? 1 : -1;
    int mine = wars[wi].score * side;
    switch (opt) {
        case 0: return 1;
        case 1:
            return mine >= 55 && wars[wi].casus != CB_RAID &&
                   prov[wars[wi].target].owner != player;
        case 2: return mine >= 30;
        case 3: return mine <= -30;
        default: return 1;
    }
}

static void try_peace(int opt) {
    int wi = realm[player].war - 1;
    int side = wars[wi].attacker == player ? 1 : -1;
    int mine = wars[wi].score * side;
    int enemy = wars[wi].attacker == player ? wars[wi].defender : wars[wi].attacker;
    switch (opt) {
        case 0:
            if (mine > -55 && mine < 65) {
                end_war(wi, -1);
                modal = MODAL_NONE;
            } else {
                chron_add("Your enemy scorns the white peace.");
                modal = MODAL_NONE;
            }
            break;
        case 1:
            if (mine >= 55) {
                end_war(wi, player);
                modal = MODAL_NONE;
            } else {
                chron_add("They will not cede the province yet.");
                modal = MODAL_NONE;
            }
            break;
        case 2:
            if (mine >= 30) {
                int gold = 80;
                realm[player].gold = (int16_t)(realm[player].gold + gold);
                realm[enemy].gold = (int16_t)imax(0, realm[enemy].gold - gold);
                realm[player].prestige = (int16_t)(realm[player].prestige + 30);
                end_war(wi, -1);
                chron_add("Tribute is paid, and the war ends.");
                modal = MODAL_NONE;
            } else {
                chron_add("They refuse to pay tribute.");
                modal = MODAL_NONE;
            }
            break;
        case 3:
            if (mine <= -30) {
                realm[player].gold = (int16_t)imax(0, realm[player].gold - 80);
                realm[enemy].gold = (int16_t)(realm[enemy].gold + 80);
                end_war(wi, -1);
                chron_add("You buy the peace with 80 gold.");
                modal = MODAL_NONE;
            } else {
                chron_add("They do not want your gold.");
                modal = MODAL_NONE;
            }
            break;
        default:
            end_war(wi, enemy);
            modal = MODAL_NONE;
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

static void open_context(int p, int mx, int my) {
    int n = 0;
    ctx_prov = p;
    ctx_x = mx;
    ctx_y = my;
    ctx_open = 1;

    if (p < 0) {
        scopy(ctx_label[n], "Close", 48);
        ctx_action[n] = 0;
        ctx_enabled[n++] = 1;
        ctx_count = n;
        return;
    }

    int owner = prov[p].owner;
    if (owner != player) {
        scopy(ctx_label[n], "Declare war...", 48);
        ctx_action[n] = 1;
        ctx_enabled[n++] = !realm[player].war;
        scopy(ctx_label[n], "Fabricate a claim (40g)", 48);
        ctx_action[n] = 2;
        ctx_enabled[n++] = !prov[p].claim && realm[player].gold >= 40;
    } else {
        scopy(ctx_label[n], "Develop province (60g)", 48);
        ctx_action[n] = 3;
        ctx_enabled[n++] = prov[p].dev < MAX_DEV;
        scopy(ctx_label[n], "Fortify province (80g)", 48);
        ctx_action[n] = 4;
        ctx_enabled[n++] = prov[p].fort < MAX_FORT;
        scopy(ctx_label[n], "Raise levies (20g)", 48);
        ctx_action[n] = 5;
        ctx_enabled[n++] = prov[p].levy >= 60 && army_at(player, p) < 0;
    }
    if (army_slot_selected() >= 0) {
        scopy(ctx_label[n], "March the host here", 48);
        ctx_action[n] = 6;
        ctx_enabled[n++] = 1;
    }
    scopy(ctx_label[n], "Close", 48);
    ctx_action[n] = 0;
    ctx_enabled[n++] = 1;
    ctx_count = n;
}

static void context_activate(int i) {
    if (i < 0 || i >= ctx_count) return;
    int action = ctx_action[i];
    int p = ctx_prov;
    ctx_open = 0;
    if (!ctx_enabled[i]) return;
    switch (action) {
        case 0: break;
        case 1: open_war_decl(p); break;
        case 2: act_fabricate(p); break;
        case 3: act_build(p); break;
        case 4: act_fortify(p); break;
        case 5: act_raise(p); break;
        case 6: {
            int a = army_slot_selected();
            if (a >= 0) { order_march(a, p); panel = PANEL_ARMY; }
            break;
        }
        default: break;
    }
}

static void handle_modal_click(const struct layout *L, int mx, int my) {
    if (modal == MODAL_EVENT) {
        int bx, by;
        modal_box(L, 480, 232, &bx, &by);
        for (int i = 0; i < 2; i++) {
            if (in_box(mx, my, bx + 12, event_option_y(by, i), 456, 28)) {
                int kind = event_kind;
                modal = MODAL_NONE;
                player_event_apply(kind, i, modal_arg);
                return;
            }
        }
        return;
    }
    if (modal == MODAL_WAR_DECL) {
        int bx, by;
        modal_box(L, 600, 316, &bx, &by);
        for (int i = 0; i < CB_COUNT; i++) {
            if (in_box(mx, my, bx + 12, by + 92 + i * 46, 576, 42)) {
                declare_player_war(i);
                return;
            }
        }
        return;
    }
    if (modal == MODAL_PEACE) {
        int bx, by;
        modal_box(L, 520, 288, &bx, &by);
        for (int i = 0; i < 5; i++) {
            if (in_box(mx, my, bx + 12, peace_option_y(by, i), 496, 30)) {
                try_peace(i);
                return;
            }
        }
    }
}

static void handle_game_click(int x, int y, int w, int h, int mx, int my) {
    struct layout L;
    compute_layout(x, y, w, h, &L);

    if (modal != MODAL_NONE) { handle_modal_click(&L, mx, my); return; }

    if (ctx_open) {
        int cw = 236;
        int ch = ctx_count * 24 + 8;
        int cx = ctx_x, cy = ctx_y;
        if (cx + cw > L.x + L.w - 4) cx = L.x + L.w - cw - 4;
        if (cy + ch > L.y + L.h - 4) cy = L.y + L.h - ch - 4;
        if (cx < L.x + 4) cx = L.x + 4;
        if (cy < L.y + 4) cy = L.y + 4;
        if (in_box(mx, my, cx, cy, cw, ch)) {
            int idx = (my - cy - 4) / 24;
            context_activate(idx);
        } else {
            ctx_open = 0;
        }
        return;
    }

    if (game_over) return;

    for (int i = 0; i < 4; i++) {
        if (in_box(mx, my, L.speed_x + i * (L.speed_w + 2), L.speed_y,
                   L.speed_w, L.speed_h)) {
            speed = i;
            last_advance = timer_get_ticks();
            return;
        }
    }
    if (in_box(mx, my, L.next_x, L.next_y, L.next_w, L.next_h)) {
        advance_month();
        return;
    }
    for (int i = 0; i < 5; i++) {
        if (in_box(mx, my, tab_x_at(&L, i), L.tab_y, L.tab_w, L.tab_h)) {
            if (panel == tab_panel[i]) panel = PANEL_NONE;
            else panel = tab_panel[i];
            return;
        }
    }
    if (in_box(mx, my, L.ruler_x, L.ruler_y, L.ruler_w, L.ruler_h)) {
        panel = (panel == PANEL_REALM) ? PANEL_NONE : PANEL_REALM;
        return;
    }

    if (panel == PANEL_DECISIONS) {
        for (int i = 0; i < DEC_COUNT; i++) {
            if (in_box(mx, my, L.panel_x + 8, dec_row_y(&L, i),
                       L.panel_w - 16, 34)) {
                dec_run(i);
                return;
            }
        }
    } else if (panel == PANEL_ARMY) {
        int shown = 0;
        for (int i = 0; i < ARMY_MAX && shown < 5; i++) {
            if (!armies[i].alive || armies[i].owner != player) continue;
            if (in_box(mx, my, L.panel_x + 8, army_row_y(&L, shown),
                       L.panel_w - 16, 34)) {
                selected_army = i;
                return;
            }
            shown++;
        }
        int by = L.panel_y + L.panel_h - 36;
        int bw = (L.panel_w - 22) / 2;
        if (in_box(mx, my, L.panel_x + 8, by, bw, 26)) { act_raise(selected_prov); return; }
        if (in_box(mx, my, L.panel_x + 14 + bw, by, bw, 26)) {
            int a = army_slot_selected();
            if (a >= 0) {
                armies[a].alive = 0;
                selected_army = -1;
                chron_add("A host is disbanded and goes home.");
            }
            return;
        }
    } else if (panel == PANEL_WAR) {
        if (realm[player].war && !game_over) {
            int wi = realm[player].war - 1;
            if (in_box(mx, my, L.panel_x + 8, L.panel_y + L.panel_h - 36,
                       L.panel_w - 16, 26) && wars[wi].months > 1) {
                modal = MODAL_PEACE;
                speed = 0;
                return;
            }
        }
    }

    /* The map itself. */
    if (in_box(mx, my, L.map_x, L.map_y, L.map_w, L.map_h)) {
        int p = province_at(mx, my);
        if (p < 0) return;
        if (selected_army >= 0 && armies[selected_army].alive) {
            int a = selected_army;
            if (p == armies[a].prov) { selected_prov = p; selected_army = -1; return; }
            order_march(a, p);
            selected_prov = p;
            panel = PANEL_ARMY;
            char line[64];
            scopy(line, "The host marches for ", 64);
            sappend(line, ck3_prov_defs[p].name, 64);
            sappend(line, ".", 64);
            chron_add(line);
            return;
        }
        select_province(p);
    }
}

void ck3_handle_mouse(int x, int y, int w, int h, int mx, int my) {
    if (screen == SCREEN_GAME) {
        handle_game_click(x, y, w, h, mx, my);
        return;
    }
    if (screen == SCREEN_PICK) {
        int pw = w > 760 ? 384 : 320;
        int px = pick_panel_x(x, w, pw);
        int py = y + 6;
        int ph = h - 12;
        int rows = pick_rows_visible(ph);
        int top = py + 44;
        for (int i = 0; i < rows; i++) {
            int r = pick_scroll + i;
            if (r >= REALM_MAX) break;
            if (in_box(mx, my, px + 6, top + i * 24, pw - 12, 22)) {
                pick_sel = r;
                return;
            }
        }
        if (in_box(mx, my, px + 8, py + ph - 28, (pw - 20) / 2, 22)) {
            screen = SCREEN_TITLE;
            menu_sel = 0;
            return;
        }
        if (in_box(mx, my, px + 12 + (pw - 20) / 2, py + ph - 28, (pw - 20) / 2, 22)) {
            start_game(pick_sel);
            return;
        }
        int p = province_at(mx, my);
        if (p >= 0) pick_sel = prov[p].owner;
        return;
    }
    if (screen == SCREEN_TITLE) {
        int pw = 460, ph = 300;
        int px = x + (w - pw) / 2;
        int py = y + (h - ph) / 2;
        for (int i = 0; i < 3; i++) {
            int iy = py + 84 + i * 32;
            if (in_box(mx, my, px + 60, iy - 6, pw - 120, 24)) {
                if (i == 0) { menu_sel = 0; start_new_chronicle(); }
                else if (i == 1 && started) { screen = SCREEN_GAME; speed = 1; }
                else if (i == 1 && save_ok && load_game()) {
                    screen = SCREEN_GAME;
                    speed = 1;
                } else if (i == 2) { screen = SCREEN_HELP; }
                return;
            }
        }
    }
}

void ck3_handle_context(int x, int y, int w, int h, int mx, int my) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if (screen != SCREEN_GAME) return;
    if (modal != MODAL_NONE) return;
    int p = province_at(mx, my);
    if (p < 0) return;
    selected_prov = p;
    open_context(p, mx, my);
}

void ck3_handle_scroll(int x, int y, int w, int h, int notches) {
    (void)x; (void)y; (void)w; (void)h;
    if (screen != SCREEN_PICK) return;
    pick_sel += notches * 3;
    if (pick_sel < 0) pick_sel = 0;
    if (pick_sel >= REALM_MAX) pick_sel = REALM_MAX - 1;
}

void ck3_handle_key(char key) {
    if (screen == SCREEN_TITLE) {
        if (key == 'w' || key == 'W' || key == (char)0x80)
            menu_sel = (menu_sel + 2) % 3;
        else if (key == 's' || key == 'S' || key == (char)0x81)
            menu_sel = (menu_sel + 1) % 3;
        else if (key == '1') start_new_chronicle();
        else if (key == '2' && started) { screen = SCREEN_GAME; speed = 1; }
        else if (key == '2' && save_ok && load_game()) {
            screen = SCREEN_GAME;
            speed = 1;
        }
        else if (key == '3') screen = SCREEN_HELP;
        else if (key == 10 || key == 13 || key == ' ' || key == 'e' || key == 'E') {
            if (menu_sel == 0) start_new_chronicle();
            else if (menu_sel == 1 && started) { screen = SCREEN_GAME; speed = 1; }
            else if (menu_sel == 1 && save_ok && load_game()) {
                screen = SCREEN_GAME;
                speed = 1;
            } else if (menu_sel == 2) screen = SCREEN_HELP;
        }
        return;
    }
    if (screen == SCREEN_PICK) {
        if (key == 'w' || key == 'W' || key == (char)0x80) {
            if (pick_sel > 0) pick_sel--;
        } else if (key == 's' || key == 'S' || key == (char)0x81) {
            if (pick_sel < REALM_MAX - 1) pick_sel++;
        } else if (key == 'a' || key == 'A' || key == (char)0x82) {
            if (pick_sel > 0) pick_sel--;
        } else if (key == 'd' || key == 'D' || key == (char)0x83) {
            if (pick_sel < REALM_MAX - 1) pick_sel++;
        } else if (key == 10 || key == 13 || key == ' ' || key == 'e' || key == 'E') {
            start_game(pick_sel);
        } else if (key == 0x1b) {
            screen = SCREEN_TITLE;
            menu_sel = 0;
        }
        int rows = 13;
        if (pick_sel < pick_scroll) pick_scroll = pick_sel;
        if (pick_sel >= pick_scroll + rows) pick_scroll = pick_sel - rows + 1;
        return;
    }
    if (screen == SCREEN_HELP) {
        screen = SCREEN_TITLE;
        return;
    }

    /* Game. */
    if (modal == MODAL_EVENT) {
        if (key == '1') {
            int kind = event_kind;
            modal = MODAL_NONE;
            player_event_apply(kind, 0, modal_arg);
        } else if (key == '2' || key == 0x1b) {
            int kind = event_kind;
            modal = MODAL_NONE;
            player_event_apply(kind, 1, modal_arg);
        }
        return;
    }
    if (modal == MODAL_WAR_DECL) {
        if (key >= '1' && key <= '4') declare_player_war(key - '1');
        else if (key == 0x1b) modal = MODAL_NONE;
        return;
    }
    if (modal == MODAL_PEACE) {
        if (key >= '1' && key <= '5') try_peace(key - '1');
        else if (key == 0x1b) modal = MODAL_NONE;
        return;
    }
    if (ctx_open) {
        if (key == 0x1b) { ctx_open = 0; return; }
    }
    if (game_over) {
        if (key == 'r' || key == 'R') start_new_chronicle();
        return;
    }

    switch (key) {
        case 'w': case 'W': case (char)0x80: move_selection(0, -1); break;
        case 's': case 'S': case (char)0x81: move_selection(0, 1); break;
        case 'a': case 'A': case (char)0x82: move_selection(-1, 0); break;
        case 'd': case 'D': case (char)0x83: move_selection(1, 0); break;
        case 'e': case 'E': case ' ': case 10: case 13: advance_month(); break;
        case 'p': case 'P': speed = speed ? 0 : 1; last_advance = timer_get_ticks(); break;
        case 'm': case 'M': map_mode = !map_mode; break;
        case 'j': case 'J': open_war_decl(selected_prov); break;
        case 'k': case 'K': act_build(selected_prov); break;
        case 'f': case 'F': act_feast(); break;
        case 'c': case 'C': act_fabricate(selected_prov); break;
        case 'g': case 'G': act_raise(selected_prov); break;
        case 't': case 'T': {
            int a = army_slot_selected();
            if (a < 0) { chron_add("Choose a host first (H cycles your hosts)."); break; }
            if (selected_prov < 0 || selected_prov >= PROV_MAX) break;
            if (selected_prov == armies[a].prov) { chron_add("The host is already there."); break; }
            order_march(a, selected_prov);
            panel = PANEL_ARMY;
            char line[64];
            scopy(line, "The host marches for ", 64);
            sappend(line, ck3_prov_defs[selected_prov].name, 64);
            sappend(line, ".", 64);
            chron_add(line);
            break;
        }
        case 'h': case 'H': {
            int first = -1, next = -1;
            int cur = army_slot_selected();
            for (int i = 0; i < ARMY_MAX; i++) {
                if (!armies[i].alive || armies[i].owner != player) continue;
                if (first < 0) first = i;
                if (cur < 0 || i > cur) { next = i; break; }
            }
            if (next < 0) next = first;
            selected_army = next;
            if (next >= 0) { selected_prov = armies[next].prov; panel = PANEL_ARMY; }
            break;
        }
        case 'v': case 'V':
            chron_add(save_game() ? "The chronicle is written to disk."
                                  : "The chronicle could not be saved.");
            save_ok = save_present();
            break;
        case 'o': case 'O': debug_overlay = !debug_overlay; break;
        case 'z': case 'Z': dump_state(); break;
        case 'y': case 'Y':
            for (int i = 0; i < 12 && !game_over; i++) advance_month();
            break;
        case '1': panel = PANEL_DECISIONS; break;
        case '2': panel = PANEL_REALM; break;
        case '3': panel = PANEL_ARMY; break;
        case '4': panel = PANEL_WAR; break;
        case '5': panel = PANEL_CHRONICLE; break;
        case '\t': {
            int order[5] = {PANEL_DECISIONS, PANEL_REALM, PANEL_ARMY, PANEL_WAR,
                            PANEL_CHRONICLE};
            int cur = -1;
            for (int i = 0; i < 5; i++) if (panel == order[i]) cur = i;
            cur = (cur + 1) % 5;
            panel = order[cur];
            break;
        }
        case 0x1b:
            if (panel != PANEL_NONE) panel = PANEL_NONE;
            else { screen = SCREEN_TITLE; menu_sel = 1; speed = 0; }
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* App entry points                                                    */
/* ------------------------------------------------------------------ */

void ck3_init(void) {
    rng_state = 0x5EED1066u;
    world_built = 0;
    year = 1066;
    month = 1;
    player = 0;
    selected_prov = 0;
    selected_army = -1;
    panel = PANEL_NONE;
    modal = MODAL_NONE;
    ctx_open = 0;
    game_over = 0;
    speed = 0;
    map_mode = 0;
    started = 0;
    chron_count = 0;
    screen = SCREEN_TITLE;
    menu_sel = 0;
    pick_sel = 0;
    pick_scroll = 0;
    map_focus_realm = -1;
    for (int i = 0; i < REALM_MAX; i++)
        for (int j = 0; j < REALM_MAX; j++) truce[i][j] = 0;
    ticker[0] = '\0';
    debug_overlay = 0;
    repaint_ms = 0;
    campaign_seed = 0x5EED1066u;
    build_world();
    init_colors();
    province_defs_init();
    save_ok = save_present();
    chron_add("1066: a new hand reaches for the map of the world.");
}

int ck3_tick(void) {
    if (screen != SCREEN_GAME || modal != MODAL_NONE || game_over) return 0;
    if (speed == 0) return 0;
    uint32_t now = timer_get_ticks();
    uint32_t period = speed == 1 ? 70u : (speed == 2 ? 34u : 16u);
    if ((uint32_t)(now - last_advance) < period) return 0;
    last_advance = now;
    advance_month();
    return 1;
}

void ck3_draw(int x, int y, int w, int h) {
    if (w < 240 || h < 180) return;
    uint32_t t0 = timer_get_ticks();
    if (screen == SCREEN_HELP) draw_help(x, y, w, h);
    else if (screen == SCREEN_TITLE) draw_title(x, y, w, h);
    else if (screen == SCREEN_PICK) draw_pick(x, y, w, h);
    else draw_game(x, y, w, h);
    /* One timer tick is 10 ms at 100 Hz; shown in the overlay so "is the map
     * repaint slow on VGA" gets a number instead of an opinion. */
    repaint_ms = (int)((timer_get_ticks() - t0) * 10u);
}

void ck3_get_size(int *w, int *h) {
    if (w) *w = 1000;
    if (h) *h = 680;
}



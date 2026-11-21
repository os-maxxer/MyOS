/* Crusader Kings III - a Solis chronicle.
 *
 * A compact feudal grand-strategy game in the spirit of the Paradox classic:
 * a map of Europe divided into provinces held by rival dynasties, a ruler
 * with gold, prestige and piety, wars fought over single provinces with a
 * war score, province upgrades, a pilgrimage, heirs and succession, and
 * random court events. Everything is turn based: one key advances a month.
 *
 * The map is drawn from hand-placed polygons in a 1000x620 map space, so it
 * reads as Europe rather than a grid: the British Isles, Iberia, France, the
 * Low Countries, Germany, the Alps and Italy each hold their provinces where
 * they belong. No floating point is used anywhere (the kernel is built with
 * -mno-sse and -mno-80387); scaling is 8.8 fixed point.
 */

#include <solis/apps/ck3.h>
#include <solis/graphics.h>

#define PROVINCE_COUNT 20
#define RULER_COUNT    4
#define MAX_DEV        9

/* Map space. Every polygon below is expressed in these units. */
#define MAP_W 1000
#define MAP_H 620

/* Land is rasterised onto this coarse grid and then grown by one cell,
 * which joins neighbouring provinces into a single coastline. */
#define LAND_W 64
#define LAND_H 36

#define C_BG       0xFF0E1520
#define C_SEA      0xFF102A3C
#define C_SEA_DEEP 0xFF0C2130
#define C_COAST    0xFF4E5741
#define C_LAND     0xFF58614A
#define C_PANEL    0xFF171A24
#define C_PANEL2   0xFF1F2331
#define C_LINE     0xFF333A4C
#define C_TEXT     0xFFE6EAF2
#define C_DIM      0xFF8A93A6
#define C_GOLD     0xFFE8C063
#define C_PIETY    0xFF9B8CE0
#define C_WAR      0xFFD1594F
#define C_OK       0xFF4FB07A
#define C_SEL      0xFFF2D48A
#define C_HEAD     0xFF6E8FD6
#define C_ACTIVE_BORDER 0xFF3D5A8C

#define T_PLAINS 0
#define T_FOREST 1
#define T_HILLS  2
#define T_MARSH  3

#define SCREEN_MENU 0
#define SCREEN_GAME 1
#define SCREEN_HELP 2

struct pt {
    int16_t x;
    int16_t y;
};

struct province {
    uint8_t  owner;
    uint8_t  terrain;
    uint8_t  dev;
    uint16_t levy;
};

struct ruler {
    const char *name;
    const char *house;
    int gold;
    int prestige;
    int piety;
    int age;
    int health;
    int heir;
    int heir_age;
};

/* The shape is listed clockwise in map space. "label" is the short name
 * drawn inside the province, "name" is the full name used in the panel. */
static const struct {
    const char *name;
    const char *label;
    uint8_t owner;
    uint8_t terrain;
    uint8_t dev;
    uint8_t count;
    struct pt shape[7];
    struct pt label_at;
} province_defs[PROVINCE_COUNT] = {
    {"Ireland", "Irel", 1, T_FOREST, 3, 5,
     {{140, 118}, {182, 108}, {196, 150}, {170, 182}, {138, 158}}, {166, 141}},
    {"Scotland", "Scot", 1, T_HILLS, 3, 5,
     {{204, 58}, {272, 48}, {292, 94}, {250, 120}, {208, 104}}, {246, 82}},
    {"England", "Engl", 1, T_PLAINS, 5, 6,
     {{208, 120}, {302, 108}, {322, 158}, {300, 206}, {240, 200}, {204, 166}},
     {262, 156}},
    {"Denmark", "Denm", 1, T_MARSH, 3, 4,
     {{470, 120}, {526, 112}, {536, 150}, {484, 162}}, {500, 134}},

    {"Flanders", "Flan", 0, T_MARSH, 4, 4,
     {{390, 214}, {438, 204}, {448, 240}, {400, 252}}, {412, 248}},
    {"Brabant", "Brab", 0, T_PLAINS, 4, 4,
     {{438, 204}, {492, 214}, {495, 258}, {448, 240}}, {470, 232}},
    {"Lotharingia", "Loth", 0, T_FOREST, 3, 5,
     {{400, 252}, {495, 258}, {505, 300}, {430, 312}, {398, 288}}, {452, 282}},
    {"Saxony", "Sax", 0, T_FOREST, 3, 4,
     {{470, 164}, {548, 158}, {558, 212}, {495, 214}}, {512, 188}},
    {"Bavaria", "Bav", 3, T_HILLS, 4, 5,
     {{505, 262}, {572, 254}, {588, 314}, {520, 326}, {505, 300}}, {545, 288}},

    {"Normandy", "Norm", 2, T_PLAINS, 4, 4,
     {{300, 218}, {362, 206}, {376, 246}, {315, 258}}, {338, 230}},
    {"Ile-de-France", "Fran", 2, T_PLAINS, 5, 4,
     {{362, 206}, {418, 220}, {424, 262}, {376, 246}}, {386, 216}},
    {"Aquitaine", "Aqui", 2, T_FOREST, 4, 5,
     {{300, 260}, {378, 254}, {396, 320}, {325, 338}, {290, 300}}, {340, 296}},
    {"Provence", "Prov", 2, T_PLAINS, 4, 5,
     {{396, 320}, {456, 306}, {470, 352}, {404, 368}, {392, 344}}, {428, 336}},

    {"Leon", "Leon", 2, T_HILLS, 3, 5,
     {{150, 350}, {214, 340}, {228, 388}, {196, 414}, {146, 398}}, {182, 372}},
    {"Castile", "Cast", 2, T_PLAINS, 4, 5,
     {{228, 388}, {300, 378}, {312, 428}, {268, 456}, {214, 410}}, {260, 410}},
    {"Aragon", "Arag", 2, T_HILLS, 3, 5,
     {{300, 378}, {372, 370}, {386, 418}, {330, 438}, {312, 428}}, {344, 398}},

    {"Tirol", "Tiro", 3, T_HILLS, 4, 4,
     {{456, 332}, {516, 322}, {530, 366}, {472, 380}}, {492, 350}},
    {"Lombardy", "Lomb", 3, T_PLAINS, 5, 4,
     {{472, 380}, {530, 366}, {546, 418}, {486, 432}}, {508, 398}},
    {"Tuscany", "Tusc", 3, T_HILLS, 4, 4,
     {{486, 432}, {546, 418}, {552, 466}, {498, 482}}, {520, 448}},
    {"Sicily", "Sici", 3, T_PLAINS, 3, 4,
     {{528, 522}, {578, 514}, {584, 550}, {530, 558}}, {556, 534}},
};

/* Unclaimed land drawn behind the provinces so the continent reads as a
 * coastline rather than a floating patch of provinces. */
/* The land behind the provinces: Britain, Ireland, Scandinavia, the
 * continental mass, Italy, Sicily, Anatolia and the African shore. The
 * provinces are drawn on top, so gaps between them read as unclaimed land
 * instead of open sea and the whole thing keeps a European outline. */
/* Land that has no province of its own: the Scandinavian peninsula,
 * Anatolia and the African shore. Everything else is land grown from the
 * provinces themselves, so no province can end up floating in open sea. */
static const struct {
    uint8_t count;
    struct pt shape[8];
} coast_defs[] = {
    /* Scandinavian peninsula */
    {6, {{506, 26}, {560, 20}, {602, 56}, {592, 118}, {546, 142}, {508, 102}}},
    /* Anatolia */
    {5, {{704, 430}, {788, 424}, {804, 464}, {736, 480}, {706, 462}}},
    /* North Africa */
    {5, {{176, 556}, {356, 544}, {468, 560}, {446, 602}, {198, 606}}},
};


static const char *const ruler_names[RULER_COUNT] = {
    "Alaric", "Otto", "Hugues", "Berthold"
};
static const char *const ruler_houses[RULER_COUNT] = {
    "Solis", "von Loon", "de Metz", "von Tirol"
};
static const char *const terrain_names[4] = {
    "Plains", "Forest", "Hills", "Marsh"
};
static const uint32_t owner_colors[RULER_COUNT] = {
    0xFF4C86D6, 0xFFC25454, 0xFF5E9B63, 0xFF9A64BC
};
static const uint32_t owner_shade[RULER_COUNT] = {
    0xFF243B57, 0xFF4A2727, 0xFF2A3F2C, 0xFF3C2C4A
};

static struct province provinces[PROVINCE_COUNT];
static struct ruler rulers[RULER_COUNT];
static struct pt centroids[PROVINCE_COUNT];
static uint8_t land_grid[LAND_H][LAND_W];

static int screen = SCREEN_MENU;
static int menu_sel = 0;
static int started;
static int sel;
static int year;
static int month;
static int war_on;
static int war_target;
static int war_enemy;
static int war_score;
static int event_open;
static int event_kind;
static int game_over;
static int months_to_event;
static uint32_t rng_state;

/* Map transform, set by draw_map and reused by the polygon helpers. */
static int map_ox;
static int map_oy;
static int map_scale;

static char chron[6][52];
static int chron_count;

/* ------------------------------------------------------------------ */
/* Small string and random helpers (the apps have no libc)             */
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
    while (src && src[j] && i < max - 1) { dst[i++] = src[j++]; }
    dst[i] = '\0';
}

static void snum(char *dst, int value, int max) {
    char tmp[12];
    int n = 0;
    int i = slen(dst);
    if (value < 0) {
        if (i < max - 1) dst[i++] = '-';
        value = -value;
    }
    if (value == 0) tmp[n++] = '0';
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (n > 0 && i < max - 1) dst[i++] = tmp[--n];
    dst[i] = '\0';
}

static uint32_t rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 16;
}

static int rnd_below(int n) {
    if (n <= 0) return 0;
    return (int)(rnd() % (uint32_t)n);
}

static int iabs(int v) {
    return v < 0 ? -v : v;
}

/* ------------------------------------------------------------------ */
/* Chronicle                                                           */
/* ------------------------------------------------------------------ */

static void chron_shift(void) {
    if (chron_count < 6) {
        chron_count++;
        return;
    }
    for (int i = 1; i < 6; i++) scopy(chron[i - 1], chron[i], 52);
}

static void chron_msg(const char *text) {
    chron_shift();
    scopy(chron[chron_count - 1], text, 52);
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static void province_refresh(int i) {
    static const int levy_bonus[4] = {10, 20, 30, 5};
    provinces[i].levy = (uint16_t)(30 + provinces[i].dev * 22 +
                                   levy_bonus[provinces[i].terrain]);
}

static int province_tax(int i) {
    int tax = 1 + provinces[i].dev / 2;
    if (provinces[i].terrain == T_PLAINS) tax++;
    if (provinces[i].owner == 0 && rulers[0].piety >= 100) tax++;
    return tax;
}

static int realm_levies(void) {
    int total = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == 0) total += provinces[i].levy;
    return total;
}

static int realm_provinces(int owner) {
    int total = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == owner) total++;
    return total;
}

static int map_in_province(int px, int py, int idx) {
    int n = province_defs[idx].count;
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        int xi = province_defs[idx].shape[i].x;
        int yi = province_defs[idx].shape[i].y;
        int xj = province_defs[idx].shape[j].x;
        int yj = province_defs[idx].shape[j].y;
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

/* Paint the province shapes into the coarse grid, then grow the result by
 * one cell so provinces that share a border also share a coastline. */
static void build_land_mask(void) {
    uint8_t grow[LAND_H][LAND_W];
    for (int y = 0; y < LAND_H; y++) {
        for (int x = 0; x < LAND_W; x++) {
            int mx = (x * MAP_W + MAP_W / 2) / LAND_W;
            int my = (y * MAP_H + MAP_H / 2) / LAND_H;
            uint8_t v = 0;
            for (int i = 0; i < PROVINCE_COUNT && !v; i++)
                if (map_in_province(mx, my, i)) v = 1;
            land_grid[y][x] = v;
        }
    }
    for (int y = 0; y < LAND_H; y++) {
        for (int x = 0; x < LAND_W; x++) {
            uint8_t v = land_grid[y][x];
            if (!v && x > 0 && land_grid[y][x - 1]) v = 1;
            if (!v && x + 1 < LAND_W && land_grid[y][x + 1]) v = 1;
            if (!v && y > 0 && land_grid[y - 1][x]) v = 1;
            if (!v && y + 1 < LAND_H && land_grid[y + 1][x]) v = 1;
            grow[y][x] = v;
        }
    }
    for (int y = 0; y < LAND_H; y++)
        for (int x = 0; x < LAND_W; x++)
            land_grid[y][x] = grow[y][x];
}

void ck3_init(void) {
    rng_state = 0x5EED1234u;
    build_land_mask();
    for (int i = 0; i < PROVINCE_COUNT; i++) {
        provinces[i].owner = province_defs[i].owner;
        provinces[i].terrain = province_defs[i].terrain;
        provinces[i].dev = province_defs[i].dev;
        province_refresh(i);

        int n = province_defs[i].count;
        int sx = 0, sy = 0;
        for (int k = 0; k < n; k++) {
            sx += province_defs[i].shape[k].x;
            sy += province_defs[i].shape[k].y;
        }
        centroids[i].x = (int16_t)(sx / n);
        centroids[i].y = (int16_t)(sy / n);

    }
    for (int i = 0; i < RULER_COUNT; i++) {
        rulers[i].name = ruler_names[i];
        rulers[i].house = ruler_houses[i];
        rulers[i].gold = i == 0 ? 120 : 90;
        rulers[i].prestige = i == 0 ? 80 : 60;
        rulers[i].piety = i == 0 ? 30 : 20;
        rulers[i].age = i == 0 ? 24 : 30 + i * 7;
        rulers[i].health = 100;
        rulers[i].heir = 0;
        rulers[i].heir_age = 0;
    }
    sel = 1;                       /* Lotharingia, the player's seat */
    year = 1066;
    month = 1;
    war_on = 0;
    war_target = -1;
    war_enemy = -1;
    war_score = 0;
    event_open = 0;
    event_kind = 0;
    game_over = 0;
    months_to_event = 3 + rnd_below(3);
    chron_count = 0;
    chron_msg("You inherit the duchy of Solis.");
    chron_msg("Rival houses covet your provinces.");
    started = 0;
    screen = SCREEN_MENU;
    menu_sel = 0;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

struct event_def {
    const char *title;
    const char *text1;
    const char *text2;
    const char *opt1;
    const char *opt2;
};

static const struct event_def events[] = {
    {"A merchant of Brabant",
     "offers a loan at a steep rate.",
     "He waits for your answer.",
     "Take 60 gold, lose prestige",
     "Decline politely"},
    {"The bishop requests",
     "a donation for the new abbey.",
     "He watches your purse.",
     "Give 40 gold for piety",
     "Refuse and lose 5 piety"},
    {"A distant claimant",
     "disputes your title to a province.",
     "Your steward looks worried.",
     "Pay 30 gold to silence him",
     "Ignore him, lose prestige"},
    {"The harvest is rich",
     "and the villages bring tribute.",
     "Your steward counts the carts.",
     "Hold a feast for prestige",
     "Store the grain as gold"},
    {"Your marshal drills",
     "the levies in the courtyard.",
     "The men are willing.",
     "Pay 40 gold, raise levies",
     "Spare the treasury"},
};

#define EVENT_COUNT ((int)(sizeof(events) / sizeof(events[0])))

static void event_apply(int kind, int choice) {
    int i;
    switch (kind) {
        case 0:
            if (choice == 0) {
                rulers[0].gold += 60;
                rulers[0].prestige -= 10;
                chron_msg("You take the merchant's loan.");
            } else {
                chron_msg("You decline the loan.");
            }
            break;
        case 1:
            if (choice == 0) {
                rulers[0].gold -= 40;
                rulers[0].piety += 35;
                chron_msg("The abbey remembers your gift.");
            } else {
                rulers[0].piety -= 5;
                chron_msg("The bishop leaves disappointed.");
            }
            break;
        case 2:
            if (choice == 0) {
                rulers[0].gold -= 30;
                rulers[0].prestige += 15;
                chron_msg("The claimant withdraws his suit.");
            } else {
                rulers[0].prestige -= 20;
                for (i = 0; i < PROVINCE_COUNT; i++)
                    if (provinces[i].owner == 0 && provinces[i].dev > 1) {
                        provinces[i].dev--;
                        province_refresh(i);
                        break;
                    }
                chron_msg("Your title is mocked at court.");
            }
            break;
        case 3:
            if (choice == 0) {
                rulers[0].prestige += 25;
                rulers[0].gold += 10;
                chron_msg("The feast is long remembered.");
            } else {
                rulers[0].gold += 45;
                chron_msg("Granaries fill, coin follows.");
            }
            break;
        default:
            if (choice == 0) {
                rulers[0].gold -= 40;
                for (i = 0; i < PROVINCE_COUNT; i++)
                    if (provinces[i].owner == 0) {
                        provinces[i].dev = (uint8_t)(provinces[i].dev < MAX_DEV
                                                         ? provinces[i].dev + 1
                                                         : provinces[i].dev);
                        province_refresh(i);
                    }
                chron_msg("The levies are drilled and ready.");
            } else {
                chron_msg("You spare the treasury.");
            }
            break;
    }
    if (rulers[0].gold < 0) rulers[0].gold = 0;
    if (rulers[0].piety < 0) rulers[0].piety = 0;
    if (rulers[0].prestige < 0) rulers[0].prestige = 0;
}

/* ------------------------------------------------------------------ */
/* Turn                                                                */
/* ------------------------------------------------------------------ */

static void end_war_win(void) {
    provinces[war_target].owner = 0;
    province_refresh(war_target);
    rulers[0].prestige += 150;
    rulers[0].gold += 25;
    chron_msg("Victory! A province joins your realm.");
    war_on = 0;
    war_target = -1;
    war_enemy = -1;
    war_score = 0;
}

static void end_war_loss(void) {
    rulers[0].prestige = rulers[0].prestige > 120 ? rulers[0].prestige - 120 : 0;
    rulers[0].gold = rulers[0].gold > 50 ? rulers[0].gold - 50 : 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == 0 && provinces[i].dev > 1) {
            provinces[i].dev--;
            province_refresh(i);
            break;
        }
    chron_msg("Defeat. Your host is scattered.");
    war_on = 0;
    war_target = -1;
    war_enemy = -1;
    war_score = 0;
}

static void player_dies(void) {
    if (rulers[0].heir) {
        chron_msg("You die. Your heir takes the seat.");
        rulers[0].age = 16;
        rulers[0].health = 100;
        rulers[0].heir = 0;
        rulers[0].heir_age = 0;
        rulers[0].prestige = rulers[0].prestige * 4 / 5;
        chron_msg("A new generation rules House Solis.");
    } else {
        game_over = 1;
        chron_msg("You die without an heir.");
        chron_msg("Your dynasty has ended.");
    }
}

static void ai_turn(void) {
    /* A rival may raid your lands when your prestige is low. */
    if (rnd_below(100) < 12) {
        int rival = 1 + rnd_below(RULER_COUNT - 1);
        if (rulers[0].prestige >= 90 && rnd_below(100) < 55) {
            rulers[0].prestige += 10;
            chron_msg("Raiders turned back at the border.");
        } else {
            int loss = 15 + rnd_below(25);
            rulers[0].gold = rulers[0].gold > loss ? rulers[0].gold - loss : 0;
            rulers[0].prestige = rulers[0].prestige > 10
                                     ? rulers[0].prestige - 10 : 0;
            char line[52];
            scopy(line, "Raiders from ", 52);
            sappend(line, rulers[rival].house, 52);
            sappend(line, " burn a village.", 52);
            chron_msg(line);
        }
    }
}

static void advance_month(void) {
    if (game_over || event_open) return;

    month++;
    if (month > 12) { month = 1; year++; }

    /* Taxes */
    int income = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++)
        if (provinces[i].owner == 0) income += province_tax(i);
    rulers[0].gold += income;
    if (rulers[0].gold > 99999) rulers[0].gold = 99999;

    /* War */
    if (war_on) {
        int mine = realm_levies();
        int theirs = provinces[war_target].levy * 2;
        /* A stronger host advances the war score; a weaker one loses ground. */
        int swing = 2 + (mine - theirs) / 12;
        if (swing < -12) swing = -12;
        if (swing > 12) swing = 12;
        war_score += swing;
        if (war_score >= 100) end_war_win();
        else if (war_score <= -100) end_war_loss();
    }

    ai_turn();

    /* Heir grows up */
    if (rulers[0].heir) rulers[0].heir_age++;

    /* Age and mortality */
    if (month == 1) rulers[0].age++;
    int risk = 0;
    if (rulers[0].age > 45) risk = (rulers[0].age - 45) * 2;
    if (rulers[0].health < 60) risk += 4;
    if (risk > 0 && rnd_below(1000) < risk) player_dies();

    /* Court events */
    if (!game_over) {
        months_to_event--;
        if (months_to_event <= 0) {
            event_kind = rnd_below(EVENT_COUNT);
            event_open = 1;
            months_to_event = 4 + rnd_below(4);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Player actions                                                      */
/* ------------------------------------------------------------------ */

static void act_declare_war(void) {
    if (game_over) return;
    if (war_on) { chron_msg("You are already at war."); return; }
    if (provinces[sel].owner == 0) { chron_msg("That province is yours."); return; }
    if (rulers[0].prestige < 25) {
        chron_msg("You need 25 prestige to press a claim.");
        return;
    }
    rulers[0].prestige -= 25;
    war_on = 1;
    war_target = sel;
    war_enemy = provinces[sel].owner;
    war_score = 0;
    char line[52];
    scopy(line, "War declared on ", 52);
    sappend(line, rulers[war_enemy].house, 52);
    chron_msg(line);
}

static void act_build(void) {
    if (provinces[sel].owner != 0) {
        chron_msg("Build only in your own provinces.");
        return;
    }
    if (provinces[sel].dev >= MAX_DEV) { chron_msg("It is fully built."); return; }
    if (rulers[0].gold < 60) { chron_msg("You need 60 gold to build."); return; }
    rulers[0].gold -= 60;
    provinces[sel].dev++;
    province_refresh(sel);
    char line[52];
    scopy(line, "Built up ", 52);
    sappend(line, province_defs[sel].name, 52);
    chron_msg(line);
}

static void act_feast(void) {
    if (rulers[0].gold < 30) { chron_msg("You need 30 gold for a feast."); return; }
    rulers[0].gold -= 30;
    rulers[0].prestige += 25;
    rulers[0].health = rulers[0].health < 95 ? rulers[0].health + 5 : 100;
    chron_msg("Your court feasts and talks of you.");
}

static void act_pilgrimage(void) {
    if (rulers[0].gold < 50) { chron_msg("A pilgrimage costs 50 gold."); return; }
    rulers[0].gold -= 50;
    rulers[0].piety += 40;
    chron_msg("You walk to the shrine and return.");
}

static void act_heir(void) {
    if (rulers[0].heir) { chron_msg("You already have an heir."); return; }
    if (rulers[0].gold < 40) { chron_msg("A marriage costs 40 gold."); return; }
    rulers[0].gold -= 40;
    rulers[0].heir = 1;
    rulers[0].heir_age = 0;
    chron_msg("A child is born to House Solis.");
}

/* Move to the closest province in the pressed direction, measured from the
 * centroids, so the arrows feel geographic on the Europe map. */
static void move_selection(int dx, int dy) {
    int best = -1;
    long best_score = 0;
    for (int i = 0; i < PROVINCE_COUNT; i++) {
        if (i == sel) continue;
        int ddx = (int)centroids[i].x - (int)centroids[sel].x;
        int ddy = (int)centroids[i].y - (int)centroids[sel].y;
        if (dx > 0 && ddx <= 6) continue;
        if (dx < 0 && ddx >= -6) continue;
        if (dy > 0 && ddy <= 6) continue;
        if (dy < 0 && ddy >= -6) continue;
        long along = dx != 0 ? (long)iabs(ddx) : (long)iabs(ddy);
        long across = dx != 0 ? (long)ddy : (long)ddx;
        long score = (long)ddx * ddx + (long)ddy * ddy + 3 * across * across;
        if (along < across) continue;      /* keep the direction dominant */
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    if (best >= 0) sel = best;
}

static void start_new_chronicle(void) {
    ck3_init();
    started = 1;
    screen = SCREEN_GAME;
}

void ck3_handle_key(char key) {
    if (screen == SCREEN_HELP) {
        screen = SCREEN_MENU;
        return;
    }
    if (screen == SCREEN_MENU) {
        if (key == 'w' || key == 'W' || key == (char)0x80) {
            menu_sel = (menu_sel + 2) % 3;
        } else if (key == 's' || key == 'S' || key == (char)0x81) {
            menu_sel = (menu_sel + 1) % 3;
        } else if (key == '1') {
            start_new_chronicle();
        } else if (key == '2') {
            if (started) screen = SCREEN_GAME;
        } else if (key == '3') {
            screen = SCREEN_HELP;
        } else if (key == 10 || key == 13 || key == ' ' || key == 'e' || key == 'E') {
            if (menu_sel == 0) start_new_chronicle();
            else if (menu_sel == 1 && started) screen = SCREEN_GAME;
            else if (menu_sel == 2) screen = SCREEN_HELP;
        }
        return;
    }

    if (key == 0x1b) {
        screen = SCREEN_MENU;
        menu_sel = 1;
        return;
    }

    if (event_open) {
        if (key == '1') { event_apply(event_kind, 0); event_open = 0; }
        else if (key == '2') { event_apply(event_kind, 1); event_open = 0; }
        return;
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
        case 'j': case 'J': act_declare_war(); break;
        case 'k': case 'K': act_build(); break;
        case 'f': case 'F': act_feast(); break;
        case 'p': case 'P': act_pilgrimage(); break;
        case 'h': case 'H': act_heir(); break;
        case 'e': case 'E': case ' ': case 10: case 13: advance_month(); break;
        case 'r': case 'R': start_new_chronicle(); break;
        default: break;
    }
}

void ck3_get_size(int *w, int *h) {
    if (w) *w = 1000;
    if (h) *h = 680;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void text(int x, int y, const char *s, uint32_t color) {
    graphics_draw_string((uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
                         s, color);
}

static void text_center(int cx, int y, const char *s, uint32_t color) {
    text(cx - slen(s) * 4, y, s, color);
}

/* Scanline fill of a screen-space polygon, one rectangle per span. */
static void fill_poly(const int *xs, const int *ys, int n, uint32_t color) {
    if (n < 3) return;
    int min_y = ys[0], max_y = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
    for (int y = min_y; y <= max_y; y++) {
        int hits[40];
        int k = 0;
        int sample = y * 2 + 1;
        for (int i = 0; i < n && k < 39; i++) {
            int j = (i + 1) % n;
            int y0 = ys[i] * 2, y1 = ys[j] * 2;
            if ((sample >= y0 && sample < y1) || (sample >= y1 && sample < y0)) {
                int x0 = xs[i] * 2, x1 = xs[j] * 2;
                hits[k++] = x0 + (sample - y0) * (x1 - x0) / (y1 - y0);
            }
        }
        for (int a = 0; a + 1 < k; a++) {
            for (int b = a + 1; b < k; b++) {
                if (hits[b] < hits[a]) {
                    int t = hits[a];
                    hits[a] = hits[b];
                    hits[b] = t;
                }
            }
        }
        for (int a = 0; a + 1 < k; a += 2) {
            int left = hits[a] / 2;
            int right = hits[a + 1] / 2;
            if (right > left)
                graphics_fill_rect(left, y, right - left, 1, color);
        }
    }
}

/* Outline of a screen-space polygon: the two edge pixels of every scanline,
 * which is far cheaper than walking each edge with rectangles. */
static void edge_poly(const int *xs, const int *ys, int n, int thick,
                      uint32_t color) {
    if (n < 3) return;
    int min_y = ys[0], max_y = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
    for (int y = min_y; y <= max_y; y++) {
        int hits[40];
        int k = 0;
        int sample = y * 2 + 1;
        for (int i = 0; i < n && k < 39; i++) {
            int j = (i + 1) % n;
            int y0 = ys[i] * 2, y1 = ys[j] * 2;
            if ((sample >= y0 && sample < y1) || (sample >= y1 && sample < y0)) {
                int x0 = xs[i] * 2, x1 = xs[j] * 2;
                hits[k++] = x0 + (sample - y0) * (x1 - x0) / (y1 - y0);
            }
        }
        for (int a = 0; a + 1 < k; a++) {
            for (int b = a + 1; b < k; b++) {
                if (hits[b] < hits[a]) {
                    int t = hits[a];
                    hits[a] = hits[b];
                    hits[b] = t;
                }
            }
        }
        for (int a = 0; a + 1 < k; a += 2) {
            int left = hits[a] / 2;
            int right = hits[a + 1] / 2;
            if (right <= left) continue;
            graphics_fill_rect(left, y, thick, 1, color);
            graphics_fill_rect(right - thick, y, thick, 1, color);
        }
    }
}

static void to_screen(const struct pt *src, int n, int *xs, int *ys) {
    for (int i = 0; i < n; i++) {
        xs[i] = map_ox + ((int)src[i].x * map_scale >> 8);
        ys[i] = map_oy + ((int)src[i].y * map_scale >> 8);
    }
}

static void draw_coast(void) {
    int xs[34], ys[34];
    for (int c = 0; c < (int)(sizeof(coast_defs) / sizeof(coast_defs[0])); c++) {
        int n = coast_defs[c].count;
        to_screen(coast_defs[c].shape, n, xs, ys);
        fill_poly(xs, ys, n, C_COAST);
    }
}

static void draw_header(int x, int y, int w) {
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, 30, C_PANEL);
    graphics_fill_rect((uint32_t)x, (uint32_t)(y + 29), (uint32_t)w, 1, C_LINE);

    text(x + 12, y + 7, "CRUSADER KINGS III", C_GOLD);
    text(x + 12 + 18 * 8, y + 7, "- a Solis chronicle", C_DIM);

    char date[32];
    scopy(date, "", 32);
    snum(date, month, 32);
    sappend(date, "/", 32);
    snum(date, year, 32);
    int dw = slen(date) * 8;
    text(x + w - dw - 12, y + 7, date, C_TEXT);
}

static void draw_province(int index, int thick) {
    int xs[8], ys[8];
    int n = province_defs[index].count;
    struct province *p = &provinces[index];
    to_screen(province_defs[index].shape, n, xs, ys);

    fill_poly(xs, ys, n, owner_shade[p->owner]);
    edge_poly(xs, ys, n, thick, index == sel ? C_SEL : owner_colors[p->owner]);

    if (war_on && index == war_target)
        graphics_fill_rect((uint32_t)(xs[0] - 2), (uint32_t)(ys[0] - 2), 5, 5,
                           C_WAR);
}

static void draw_labels(void) {
    for (int i = 0; i < PROVINCE_COUNT; i++) {
        int lx = map_ox + ((int)province_defs[i].label_at.x * map_scale >> 8);
        int ly = map_oy + ((int)province_defs[i].label_at.y * map_scale >> 8);
        const char *label = province_defs[i].label;
        int w = slen(label) * 8;
        int cx = lx - w / 2;
        uint32_t color = i == sel ? C_SEL : C_TEXT;
        /* A dark plate keeps the name readable over any owner colour. */
        graphics_fill_rect((uint32_t)(cx - 2), (uint32_t)(ly - 2),
                           (uint32_t)(w + 4), 12, 0xAA0A0F18);
        text(cx, ly, label, color);
    }
}

static void draw_map(int x, int y, int w, int h) {
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_SEA);
    for (int i = 0; i < 6; i++) {
        int by = y + (h * i) / 6;
        graphics_fill_rect((uint32_t)x, (uint32_t)by, (uint32_t)w,
                           (uint32_t)(h / 12), C_SEA_DEEP);
    }

    int sx = (w << 8) / MAP_W;
    int sy = (h << 8) / MAP_H;
    map_scale = sx < sy ? sx : sy;
    int used_w = (MAP_W * map_scale) >> 8;
    int used_h = (MAP_H * map_scale) >> 8;
    map_ox = x + (w - used_w) / 2;
    map_oy = y + (h - used_h) / 2;

    int cell_w = ((MAP_W / LAND_W) * map_scale >> 8) + 1;
    int cell_h = ((MAP_H / LAND_H) * map_scale >> 8) + 1;
    for (int gy = 0; gy < LAND_H; gy++) {
        for (int gx = 0; gx < LAND_W; gx++) {
            if (!land_grid[gy][gx]) continue;
            int px = map_ox + (((gx * MAP_W / LAND_W) * map_scale) >> 8);
            int py = map_oy + (((gy * MAP_H / LAND_H) * map_scale) >> 8);
            graphics_fill_rect((uint32_t)px, (uint32_t)py,
                               (uint32_t)cell_w, (uint32_t)cell_h, C_COAST);
        }
    }

    draw_coast();
    for (int i = 0; i < PROVINCE_COUNT; i++) draw_province(i, 1);
    if (sel >= 0 && sel < PROVINCE_COUNT) draw_province(sel, 2);
    draw_labels();
}

static void draw_stat_row(int x, int y, const char *label, int value,
                          uint32_t color) {
    text(x, y, label, C_DIM);
    char line[24];
    scopy(line, "", 24);
    snum(line, value, 24);
    text(x + 76, y, line, color);
}

static void draw_side(int x, int y, int w, int h) {
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_PANEL);
    graphics_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_LINE);

    int px = x + 10;
    int py = y + 8;

    char line[52];
    scopy(line, "HOUSE ", 52);
    sappend(line, rulers[0].house, 52);
    text(px, py, line, C_HEAD);
    py += 18;

    scopy(line, rulers[0].name, 52);
    sappend(line, ", age ", 52);
    snum(line, rulers[0].age, 52);
    text(px, py, line, C_TEXT);
    py += 24;

    draw_stat_row(px, py, "Gold", rulers[0].gold, C_GOLD); py += 17;
    draw_stat_row(px, py, "Prestige", rulers[0].prestige, C_TEXT); py += 17;
    draw_stat_row(px, py, "Piety", rulers[0].piety, C_PIETY); py += 17;
    draw_stat_row(px, py, "Levies", realm_levies(), C_OK); py += 17;
    draw_stat_row(px, py, "Provinces", realm_provinces(0), C_TEXT); py += 22;

    if (rulers[0].heir) {
        scopy(line, "Heir age ", 52);
        snum(line, rulers[0].heir_age, 52);
        text(px, py, line, C_OK);
    } else {
        text(px, py, "No heir - press H", C_WAR);
    }
    py += 24;

    graphics_fill_rect((uint32_t)px, (uint32_t)py, (uint32_t)(w - 20), 1, C_LINE);
    py += 8;

    text(px, py, "SELECTED PROVINCE", C_HEAD);
    py += 17;
    text(px, py, province_defs[sel].name, C_TEXT); py += 15;
    scopy(line, terrain_names[provinces[sel].terrain], 52);
    sappend(line, "   tax ", 52);
    snum(line, province_tax(sel), 52);
    text(px, py, line, C_DIM); py += 15;
    scopy(line, "levies ", 52);
    snum(line, provinces[sel].levy, 52);
    sappend(line, "   dev ", 52);
    snum(line, provinces[sel].dev, 52);
    text(px, py, line, C_DIM); py += 15;
    if (provinces[sel].owner == 0) {
        text(px, py, "Held by you", C_OK);
    } else {
        scopy(line, "Held by ", 52);
        sappend(line, rulers[provinces[sel].owner].house, 52);
        text(px, py, line, owner_colors[provinces[sel].owner]);
    }
    py += 22;

    if (war_on) {
        graphics_fill_rect((uint32_t)px, (uint32_t)py, (uint32_t)(w - 20), 44, C_PANEL2);
        scopy(line, "WAR vs ", 52);
        sappend(line, rulers[war_enemy].house, 52);
        text(px + 6, py + 5, line, C_WAR);
        int bar_w = w - 32;
        int score = war_score;
        if (score < 0) score = 0;
        if (score > 100) score = 100;
        graphics_fill_rect((uint32_t)(px + 6), (uint32_t)(py + 26),
                           (uint32_t)bar_w, 8, C_LINE);
        graphics_fill_rect((uint32_t)(px + 6), (uint32_t)(py + 26),
                           (uint32_t)(bar_w * score / 100), 8, C_OK);
        py += 50;
    } else {
        py += 2;
    }

    /* Chronicle */
    text(px, py, "CHRONICLE", C_HEAD);
    py += 15;
    int lines = (y + h - 8 - py) / 15;
    if (lines > chron_count) lines = chron_count;
    if (lines > 6) lines = 6;
    for (int i = 0; i < lines; i++) {
        int idx = chron_count - lines + i;
        char clipped[32];
        if (idx < 0) idx = 0;
        scopy(clipped, chron[idx], 32);
        if (slen(clipped) > 28) clipped[28] = '\0';
        text(px, py, clipped, i == lines - 1 ? C_TEXT : C_DIM);
        py += 15;
    }
}

static void draw_event(int x, int y, int w, int h) {
    const struct event_def *e = &events[event_kind];
    int bw = 440;
    int bh = 168;
    if (bw > w - 20) bw = w - 20;
    int bx = x + (w - bw) / 2;
    int by = y + (h - bh) / 2;

    graphics_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh,
                       0xFF101420);
    graphics_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh, C_SEL);
    graphics_fill_rect((uint32_t)(bx + 1), (uint32_t)(by + 1),
                       (uint32_t)(bw - 2), 26, C_PANEL2);

    text(bx + 10, by + 6, e->title, C_SEL);
    text(bx + 10, by + 40, e->text1, C_TEXT);
    text(bx + 10, by + 56, e->text2, C_DIM);
    text(bx + 10, by + 88, "[1]", C_OK);
    text(bx + 40, by + 88, e->opt1, C_TEXT);
    text(bx + 10, by + 112, "[2]", C_WAR);
    text(bx + 40, by + 112, e->opt2, C_TEXT);
    text(bx + 10, by + 140, "Choose one to continue the month.", C_DIM);
}

static void draw_menu(int x, int y, int w, int h) {
    draw_map(x, y, w, h);
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                       0x22060A12);

    int pw = 460;
    int ph = 268;
    if (pw > w - 40) pw = w - 40;
    int px = x + (w - pw) / 2;
    int py = y + (h - ph) / 2;

    graphics_fill_rect((uint32_t)(px + 4), (uint32_t)(py + 5),
                       (uint32_t)pw, (uint32_t)ph, 0x66070A10);
    graphics_fill_rect((uint32_t)px, (uint32_t)py, (uint32_t)pw, (uint32_t)ph,
                       0xF00D1119);
    graphics_draw_rect((uint32_t)px, (uint32_t)py, (uint32_t)pw, (uint32_t)ph,
                       C_GOLD);
    graphics_fill_rect((uint32_t)(px + 1), (uint32_t)(py + 1),
                       (uint32_t)(pw - 2), 54, 0xF0161B26);
    graphics_fill_rect((uint32_t)(px + 1), (uint32_t)(py + 55),
                       (uint32_t)(pw - 2), 1, C_LINE);

    /* A small crown above the title, drawn from rectangles. */
    int cx = px + pw / 2;
    int cy = py + 14;
    graphics_fill_rect((uint32_t)(cx - 30), (uint32_t)(cy + 14), 60, 5, C_GOLD);
    graphics_fill_rect((uint32_t)(cx - 30), (uint32_t)cy, 6, 16, C_GOLD);
    graphics_fill_rect((uint32_t)(cx - 3), (uint32_t)(cy - 6), 6, 22, C_GOLD);
    graphics_fill_rect((uint32_t)(cx + 24), (uint32_t)cy, 6, 16, C_GOLD);
    graphics_fill_rect((uint32_t)(cx - 22), (uint32_t)(cy + 6), 8, 4, C_GOLD);
    graphics_fill_rect((uint32_t)(cx + 14), (uint32_t)(cy + 6), 8, 4, C_GOLD);

    text_center(cx, py + 40, "CRUSADER KINGS III", C_GOLD);

    const char *items[3] = {
        "New chronicle",
        "Continue chronicle",
        "How to play"
    };
    int iy = py + 78;
    for (int i = 0; i < 3; i++) {
        int disabled = (i == 1 && !started);
        uint32_t color = disabled ? 0xFF4A5262
                                  : (i == menu_sel ? C_SEL : C_TEXT);
        if (i == menu_sel) {
            graphics_fill_rect((uint32_t)(px + 60), (uint32_t)(iy - 4),
                               (uint32_t)(pw - 120), 22, 0xFF1D2536);
            graphics_draw_rect((uint32_t)(px + 60), (uint32_t)(iy - 4),
                               (uint32_t)(pw - 120), 22, C_ACTIVE_BORDER);
        }
        text_center(cx, iy, items[i], color);
        iy += 30;
    }

    char foot[64];
    scopy(foot, "1066 - the map of Europe awaits", 64);
    text_center(cx, py + ph - 44, foot, C_DIM);
    text_center(cx, py + ph - 26, "W/S choose   Enter select   Esc returns here", C_DIM);
}

static void draw_help(int x, int y, int w, int h) {
    static const char *const lines[] = {
        "W A S D      move the selection across Europe",
        "J            declare war on the selected province",
        "K            build up the selected province (60 gold)",
        "F            hold a feast (30 gold, +prestige)",
        "P            go on pilgrimage (50 gold, +piety)",
        "H            marry and beget an heir (40 gold)",
        "E or Space   advance one month",
        "R            begin a new chronicle",
        "Esc          return to the main menu",
        "",
        "Hold every province you can: taxes pay for wars, wars win",
        "provinces, and provinces raise the levies that win wars.",
    };
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_BG);

    int px = x + 40;
    int py = y + 40;
    text(px, py, "HOW TO PLAY", C_GOLD);
    py += 30;
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    for (int i = 0; i < n; i++) {
        text(px, py, lines[i], i == 7 || i == 8 ? C_TEXT : C_DIM);
        py += 18;
    }
    text(px, py + 16, "Press any key to return to the menu.", C_SEL);
}

void ck3_draw(int x, int y, int w, int h) {
    if (w < 240 || h < 180) return;

    if (screen == SCREEN_HELP) {
        draw_help(x, y, w, h);
        return;
    }
    if (screen == SCREEN_MENU) {
        draw_menu(x, y, w, h);
        return;
    }

    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_BG);
    draw_header(x, y, w);

    int top = y + 34;
    int body_h = h - 34 - 22;
    int map_w = w * 74 / 100;
    int side_w = w - map_w - 16;

    draw_map(x + 8, top, map_w - 8, body_h);
    draw_side(x + map_w + 8, top, side_w, body_h);

    /* Footer controls */
    int fy = y + h - 18;
    graphics_fill_rect((uint32_t)x, (uint32_t)(fy - 4), (uint32_t)w, 1, C_LINE);
    text(x + 10, fy,
         "WASD move  J war  K build  F feast  P pilgrim  H heir  E month  R restart  Esc menu",
         C_DIM);

    if (event_open) draw_event(x, top, w, body_h);

    if (game_over) {
        int bw = 400, bh = 130;
        int bx = x + (w - bw) / 2;
        int by = y + (h - bh) / 2;
        graphics_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh,
                           0xFF0E1018);
        graphics_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)bh, C_WAR);
        text(bx + 20, by + 16, "THE DYNASTY ENDS", C_WAR);
        text(bx + 20, by + 44, "No heir survives to hold your seat.", C_TEXT);
        char line[40];
        scopy(line, "Final prestige: ", 40);
        snum(line, rulers[0].prestige, 40);
        text(bx + 20, by + 68, line, C_DIM);
        text(bx + 20, by + 96, "Press R to begin a new chronicle.", C_SEL);
    }
}

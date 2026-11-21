/* Crusader Kings III - a Solis chronicle.
 *
 * A compact feudal grand-strategy game in the spirit of the Paradox classic:
 * a map of counties held by rival dynasties, a ruler with gold, prestige and
 * piety, wars fought over single counties with a war score, county upgrades,
 * a pilgrimage, heirs and succession, and random court events. Everything is
 * turn based: one key advances a month.
 */

#include <solis/apps/ck3.h>
#include <solis/graphics.h>

#define COLS          5
#define ROWS          4
#define COUNTY_COUNT  (COLS * ROWS)
#define RULER_COUNT   4
#define MAX_DEV       9

#define C_BG       0xFF12141C
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

#define T_PLAINS 0
#define T_FOREST 1
#define T_HILLS  2
#define T_MARSH  3

struct county {
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

static const struct {
    const char *name;
    uint8_t owner;
    uint8_t terrain;
    uint8_t dev;
} county_defs[COUNTY_COUNT] = {
    {"Ardenne",  0, T_FOREST, 4}, {"Valoise",  0, T_PLAINS, 5},
    {"Brabant",  0, T_PLAINS, 4}, {"Hainaut",  0, T_FOREST, 3},
    {"Loon",     1, T_MARSH,  3},
    {"Namur",    0, T_HILLS,  3}, {"Liege",    2, T_PLAINS, 5},
    {"Luxem",    1, T_FOREST, 4}, {"Barrois",  1, T_HILLS,  3},
    {"Verdun",   3, T_FOREST, 3},
    {"Toul",     2, T_HILLS,  4}, {"Metz",     2, T_PLAINS, 5},
    {"Sundgau",  3, T_FOREST, 3}, {"Alsace",   1, T_PLAINS, 4},
    {"Breisgau", 3, T_HILLS,  3},
    {"Zurich",   2, T_PLAINS, 4}, {"Thurgau",  3, T_FOREST, 3},
    {"Chur",     3, T_HILLS,  2}, {"Tirol",    1, T_HILLS,  3},
    {"Verona",   2, T_PLAINS, 4},
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
    0xFF3D6FB8, 0xFFB24A4A, 0xFF4F8A57, 0xFF8A5AA8
};
static const uint32_t owner_shade[RULER_COUNT] = {
    0xFF1C2C46, 0xFF40201F, 0xFF1F3323, 0xFF33243F
};

static struct county counties[COUNTY_COUNT];
static struct ruler rulers[RULER_COUNT];

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

static void county_refresh(int i) {
    static const int levy_bonus[4] = {10, 20, 30, 5};
    counties[i].levy = (uint16_t)(30 + counties[i].dev * 22 +
                                  levy_bonus[counties[i].terrain]);
}

static int county_tax(int i) {
    int tax = 1 + counties[i].dev / 2;
    if (counties[i].terrain == T_PLAINS) tax++;
    if (counties[i].owner == 0 && rulers[0].piety >= 100) tax++;
    return tax;
}

static int realm_levies(void) {
    int total = 0;
    for (int i = 0; i < COUNTY_COUNT; i++)
        if (counties[i].owner == 0) total += counties[i].levy;
    return total;
}

static int realm_counties(int owner) {
    int total = 0;
    for (int i = 0; i < COUNTY_COUNT; i++)
        if (counties[i].owner == owner) total++;
    return total;
}

void ck3_init(void) {
    rng_state = 0x5EED1234u;
    for (int i = 0; i < COUNTY_COUNT; i++) {
        counties[i].owner = county_defs[i].owner;
        counties[i].terrain = county_defs[i].terrain;
        counties[i].dev = county_defs[i].dev;
        county_refresh(i);
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
    sel = 0;
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
    chron_msg("Rival houses covet your counties.");
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
    {"A merchant of Liege",
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
     "disputes your title to a county.",
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
                for (i = 0; i < COUNTY_COUNT; i++)
                    if (counties[i].owner == 0 && counties[i].dev > 1) {
                        counties[i].dev--;
                        county_refresh(i);
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
                for (i = 0; i < COUNTY_COUNT; i++)
                    if (counties[i].owner == 0) {
                        counties[i].dev = (uint8_t)(counties[i].dev < MAX_DEV
                                                        ? counties[i].dev + 1
                                                        : counties[i].dev);
                        county_refresh(i);
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
    counties[war_target].owner = 0;
    county_refresh(war_target);
    rulers[0].prestige += 150;
    rulers[0].gold += 25;
    chron_msg("Victory! A county joins your realm.");
    war_on = 0;
    war_target = -1;
    war_enemy = -1;
    war_score = 0;
}

static void end_war_loss(void) {
    rulers[0].prestige = rulers[0].prestige > 120 ? rulers[0].prestige - 120 : 0;
    rulers[0].gold = rulers[0].gold > 50 ? rulers[0].gold - 50 : 0;
    for (int i = 0; i < COUNTY_COUNT; i++)
        if (counties[i].owner == 0 && counties[i].dev > 1) {
            counties[i].dev--;
            county_refresh(i);
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
    for (int i = 0; i < COUNTY_COUNT; i++)
        if (counties[i].owner == 0) income += county_tax(i);
    rulers[0].gold += income;
    if (rulers[0].gold > 99999) rulers[0].gold = 99999;

    /* War */
    if (war_on) {
        int mine = realm_levies();
        int theirs = counties[war_target].levy * 2;
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
    if (counties[sel].owner == 0) { chron_msg("That county is yours."); return; }
    if (rulers[0].prestige < 25) {
        chron_msg("You need 25 prestige to press a claim.");
        return;
    }
    rulers[0].prestige -= 25;
    war_on = 1;
    war_target = sel;
    war_enemy = counties[sel].owner;
    war_score = 0;
    char line[52];
    scopy(line, "War declared on ", 52);
    sappend(line, rulers[war_enemy].house, 52);
    chron_msg(line);
}

static void act_build(void) {
    if (counties[sel].owner != 0) {
        chron_msg("Build only in your own counties.");
        return;
    }
    if (counties[sel].dev >= MAX_DEV) { chron_msg("The county is fully built."); return; }
    if (rulers[0].gold < 60) { chron_msg("You need 60 gold to build."); return; }
    rulers[0].gold -= 60;
    counties[sel].dev++;
    county_refresh(sel);
    char line[52];
    scopy(line, "Built up ", 52);
    sappend(line, county_defs[sel].name, 52);
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

static void move_selection(int dx, int dy) {
    int cx = sel % COLS;
    int cy = sel / COLS;
    cx += dx;
    cy += dy;
    if (cx < 0) cx = COLS - 1;
    if (cx >= COLS) cx = 0;
    if (cy < 0) cy = ROWS - 1;
    if (cy >= ROWS) cy = 0;
    sel = cy * COLS + cx;
}

void ck3_handle_key(char key) {
    if (event_open) {
        if (key == '1') { event_apply(event_kind, 0); event_open = 0; }
        else if (key == '2') { event_apply(event_kind, 1); event_open = 0; }
        return;
    }
    if (game_over) {
        if (key == 'r' || key == 'R') ck3_init();
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
        case 'e': case 'E': case ' ': case 13: advance_month(); break;
        case 'r': case 'R': ck3_init(); break;
        default: break;
    }
}

void ck3_get_size(int *w, int *h) {
    if (w) *w = 980;
    if (h) *h = 660;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void text(int x, int y, const char *s, uint32_t color) {
    graphics_draw_string((uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
                         s, color);
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

static void draw_tile(int x, int y, int w, int h, int index) {
    struct county *c = &counties[index];
    uint32_t base = owner_shade[c->owner];
    uint32_t edge = owner_colors[c->owner];

    graphics_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                       (uint32_t)(w - 2), (uint32_t)(h - 2), base);
    graphics_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                       index == sel ? C_SEL : C_LINE);
    if (index == sel) {
        graphics_draw_rect((uint32_t)(x + 1), (uint32_t)(y + 1),
                           (uint32_t)(w - 2), (uint32_t)(h - 2), C_SEL);
    }
    graphics_fill_rect((uint32_t)(x + 1), (uint32_t)(y + 1), 5,
                       (uint32_t)(h - 2), edge);

    if (war_on && index == war_target)
        graphics_fill_rect((uint32_t)(x + 1), (uint32_t)(y + h - 4),
                           (uint32_t)(w - 2), 3, C_WAR);

    text(x + 10, y + 6, county_defs[index].name, C_TEXT);

    char line[40];
    scopy(line, terrain_names[c->terrain], 40);
    text(x + 10, y + 24, line, C_DIM);

    scopy(line, "DEV ", 40);
    snum(line, c->dev, 40);
    sappend(line, "  LEV ", 40);
    snum(line, c->levy, 40);
    text(x + 10, y + 40, line, C_DIM);

    if (c->owner != 0)
        text(x + 10, y + 56, ruler_houses[c->owner], owner_colors[c->owner]);
    else if (h > 70)
        text(x + 10, y + 56, "Your demesne", C_OK);
}

static void draw_map(int x, int y, int w, int h) {
    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_BG);
    int tw = w / COLS;
    int th = h / ROWS;
    if (tw < 40 || th < 40) return;
    for (int i = 0; i < COUNTY_COUNT; i++) {
        int cx = x + (i % COLS) * tw;
        int cy = y + (i / COLS) * th;
        draw_tile(cx, cy, tw, th, i);
    }
}

static void draw_stat_row(int x, int y, const char *label, int value,
                          uint32_t color) {
    text(x, y, label, C_DIM);
    char line[24];
    scopy(line, "", 24);
    snum(line, value, 24);
    text(x + 84, y, line, color);
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
    py += 26;

    draw_stat_row(px, py, "Gold", rulers[0].gold, C_GOLD); py += 18;
    draw_stat_row(px, py, "Prestige", rulers[0].prestige, C_TEXT); py += 18;
    draw_stat_row(px, py, "Piety", rulers[0].piety, C_PIETY); py += 18;
    draw_stat_row(px, py, "Levies", realm_levies(), C_OK); py += 18;
    draw_stat_row(px, py, "Counties", realm_counties(0), C_TEXT); py += 24;

    if (rulers[0].heir) {
        scopy(line, "Heir age ", 52);
        snum(line, rulers[0].heir_age, 52);
        text(px, py, line, C_OK);
    } else {
        text(px, py, "No heir - press H", C_WAR);
    }
    py += 26;

    graphics_fill_rect((uint32_t)px, (uint32_t)py, (uint32_t)(w - 20), 1, C_LINE);
    py += 8;

    text(px, py, "SELECTED COUNTY", C_HEAD);
    py += 18;
    text(px, py, county_defs[sel].name, C_TEXT); py += 16;
    scopy(line, terrain_names[counties[sel].terrain], 52);
    sappend(line, "  tax ", 52);
    snum(line, county_tax(sel), 52);
    text(px, py, line, C_DIM); py += 16;
    if (counties[sel].owner == 0) {
        text(px, py, "Held by you", C_OK);
    } else {
        scopy(line, "Held by ", 52);
        sappend(line, rulers[counties[sel].owner].house, 52);
        text(px, py, line, owner_colors[counties[sel].owner]);
    }
    py += 24;

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
        py += 52;
    } else {
        py += 4;
    }

    /* Chronicle */
    text(px, py, "CHRONICLE", C_HEAD);
    py += 16;
    int lines = (y + h - 10 - py) / 16;
    if (lines > chron_count) lines = chron_count;
    if (lines > 6) lines = 6;
    for (int i = 0; i < lines; i++) {
        int idx = chron_count - lines + i;
        if (idx < 0) idx = 0;
        text(px, py, chron[idx], i == lines - 1 ? C_TEXT : C_DIM);
        py += 16;
    }
}

static void draw_event(int x, int y, int w, int h) {
    const struct event_def *e = &events[event_kind];
    int bw = 420;
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

void ck3_draw(int x, int y, int w, int h) {
    if (w < 200 || h < 160) return;

    graphics_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, C_BG);
    draw_header(x, y, w);

    int top = y + 34;
    int body_h = h - 34 - 22;
    int map_w = w * 62 / 100;
    int side_w = w - map_w - 16;

    draw_map(x + 8, top, map_w - 8, body_h);
    draw_side(x + map_w + 8, top, side_w, body_h);

    /* Footer controls */
    int fy = y + h - 18;
    graphics_fill_rect((uint32_t)x, (uint32_t)(fy - 4), (uint32_t)w, 1, C_LINE);
    text(x + 10, fy, "WASD move  J war  K build  F feast  P pilgrim  H heir  E month  R restart",
         C_DIM);

    if (event_open) draw_event(x, top, w, body_h);

    if (game_over) {
        int bw = 380, bh = 130;
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

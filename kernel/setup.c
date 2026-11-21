#include <solis/setup.h>
#include <solis/solfs.h>
#include <solis/rtc.h>
#include <solis/gui.h>
#include <solis/timer.h>
#include <solis/console.h>
#include <solis/vfs.h>
#include <solis/spx.h>

#define SETUP_PATH "settings/account"
#define SETUP_MAGIC 0x53455450u   /* "SETP" */
#define SETUP_VERSION 2
#define SETUP_V1_SIZE 48
#define SETUP_PENDING_PATH "settings/edition.next"

struct setup_record {
    uint32_t magic;
    uint16_t version;
    uint8_t installed;
    uint8_t theme;
    uint8_t timezone;
    uint8_t reserved[3];
    uint32_t salt;
    uint64_t hash;
    char username[SETUP_USERNAME_MAX];
    uint8_t edition;
    uint8_t guest;
    uint8_t reserved2[2];
} __attribute__((packed));

static struct setup_record record;
static bool setup_loaded = false;

static int setup_strlen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void setup_copy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) src = "";
    for (; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool setup_username_valid(const char *name) {
    int length = setup_strlen(name);
    if (length <= 0 || length >= SETUP_USERNAME_MAX) return false;
    for (int i = 0; i < length; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return false;
    }
    return true;
}

static bool setup_theme_valid(int theme) {
    switch (theme) {
        case GUI_THEME_SOLID:
        case GUI_THEME_STARFIELD:
        case GUI_THEME_SUNSET:
        case GUI_THEME_CHERRY_BLOSSOM:
        case GUI_THEME_GNOME:
        case GUI_THEME_VOXEL:
        case GUI_THEME_BORED:
        case GUI_THEME_AURORA:
        case GUI_THEME_SOLIS_UI_ENGINE:
            return true;
        default:
            return false;
    }
}

static bool setup_edition_valid(int edition) {
    return edition >= SETUP_EDITION_MINIMAL &&
           edition <= SETUP_EDITION_EXTRA;
}

/* A small salted hash is enough to keep the password out of the on-disk
 * record; Solis has no filesystem permissions yet, so this is not a security
 * boundary. The password itself is never stored or logged. */
static uint64_t setup_hash_password(const char *password, uint32_t salt) {
    uint64_t hash = 1469598103934665603ULL;
    hash ^= ((uint64_t)salt * 0x9E3779B97F4A7C15ULL);
    while (password && *password) {
        hash ^= (uint8_t)*password++;
        hash *= 1099511628211ULL;
    }
    hash ^= hash >> 33;
    hash *= 0xFF51AFD7ED558CCDULL;
    hash ^= hash >> 33;
    return hash;
}

static uint32_t setup_new_salt(void) {
    uint64_t value = ((uint64_t)timer_get_ticks() << 32) ^
                     solfs_get_machine_id();
    value ^= value >> 33;
    value *= 0xFF51AFD7ED558CCDULL;
    value ^= value >> 33;
    uint32_t salt = (uint32_t)value;
    return salt ? salt : 0x51A7C0DEu;
}

static int setup_store_record(const struct setup_record *next) {
    if (!next) return -1;
    int fd = solfs_open(SETUP_PATH);
    if (fd < 0) fd = solfs_create(SETUP_PATH);
    if (fd < 0) return -1;
    if (solfs_write(fd, (const uint8_t *)next, sizeof(*next)) !=
        (int)sizeof(*next))
        return -1;
    return 0;
}

static int setup_pending_value(void) {
    int fd = solfs_open(SETUP_PENDING_PATH);
    if (fd < 0) return -1;
    uint8_t value = 0;
    if (solfs_read(fd, &value, 1) != 1) return -1;
    if (!setup_edition_valid(value)) return -1;
    return (int)value;
}

static void setup_clear_pending(void) {
    solfs_delete(SETUP_PENDING_PATH);
}

void setup_init(void) {
    uint8_t *raw = (uint8_t *)&record;
    for (uint32_t i = 0; i < sizeof(record); i++) raw[i] = 0;

    record.theme = GUI_THEME_SOLIS_UI_ENGINE;
    int tz = rtc_get_timezone();
    if (tz < 0 || tz >= RTC_MAX_TIMEZONES) tz = 0;
    record.timezone = (uint8_t)tz;
    setup_loaded = false;

    int fd = solfs_open(SETUP_PATH);
    int got = fd >= 0 ? solfs_read(fd, raw, sizeof(record)) : -1;
    if (fd >= 0 &&
        (got == (int)sizeof(record) || got == SETUP_V1_SIZE) &&
        record.magic == SETUP_MAGIC &&
        (record.version == 1 || record.version == SETUP_VERSION) &&
        record.installed &&
        record.username[0] &&
        setup_theme_valid(record.theme) &&
        record.timezone < RTC_MAX_TIMEZONES) {
        if (got == SETUP_V1_SIZE || !setup_edition_valid(record.edition))
            record.edition = SETUP_EDITION_DEFAULT;
        if (got == SETUP_V1_SIZE) record.guest = 0;
        record.username[SETUP_USERNAME_MAX - 1] = '\0';
        setup_loaded = true;
        rtc_set_timezone(record.timezone);
    } else {
        for (uint32_t i = 0; i < sizeof(record); i++) raw[i] = 0;
        record.magic = SETUP_MAGIC;
        record.version = SETUP_VERSION;
        record.theme = GUI_THEME_SOLIS_UI_ENGINE;
        record.timezone = (uint8_t)tz;
        record.edition = SETUP_EDITION_DEFAULT;
    }
}

bool setup_is_installed(void) {
    return setup_loaded;
}

const char *setup_username(void) {
    if (setup_loaded && record.username[0]) return record.username;
    return "Local user";
}

int setup_theme(void) {
    return setup_loaded ? record.theme : GUI_THEME_SOLIS_UI_ENGINE;
}

int setup_timezone(void) {
    return setup_loaded ? record.timezone : rtc_get_timezone();
}

int setup_edition(void) {
    return setup_loaded ? record.edition : SETUP_EDITION_DEFAULT;
}

int setup_pending_edition(void) {
    int pending = setup_pending_value();
    return pending >= 0 ? pending : setup_edition();
}

int setup_request_edition(int edition) {
    if (!setup_loaded || !setup_edition_valid(edition)) return -1;
    if (edition == record.edition) {
        setup_clear_pending();
        return 0;
    }

    int fd = solfs_open(SETUP_PENDING_PATH);
    if (fd < 0) fd = solfs_create(SETUP_PENDING_PATH);
    if (fd < 0) return -1;
    uint8_t value = (uint8_t)edition;
    if (solfs_write(fd, &value, 1) != 1) return -1;
    console_write("[SETUP] edition change queued for next start\n");
    return 0;
}

bool setup_is_guest(void) {
    return setup_loaded && record.guest != 0;
}

bool setup_authenticate(const char *password) {
    if (!setup_loaded || !password) return false;
    if (record.guest) return false;
    uint64_t hash = setup_hash_password(password, record.salt);
    return (record.hash ^ hash) == 0;
}

static int setup_write_account(const char *username, const char *password,
                               int theme, int timezone, int edition, bool guest) {
    int pass_len = setup_strlen(password);
    if (!setup_username_valid(username)) return -1;
    if (!guest && (pass_len < 4 || pass_len >= SETUP_PASSWORD_MAX)) return -1;
    if (!setup_theme_valid(theme)) return -1;
    if (timezone < 0 || timezone >= RTC_MAX_TIMEZONES) return -1;
    if (!setup_edition_valid(edition)) return -1;

    /* A fresh install always wins over a change queued before the install. */
    setup_clear_pending();

    if (!solfs_is_mounted()) {
        console_write("[SETUP] blank disk detected; formatting SOLFS\n");
        if (solfs_format() != 0) {
            console_write("[SETUP] no usable blank Solis disk\n");
            return SETUP_INSTALL_NO_DISK;
        }
        vfs_init();
    }

    if (solfs_open("settings") < 0) {
        if (solfs_mkdir("settings") < 0) {
            console_write("[SETUP] could not create settings directory\n");
            return SETUP_INSTALL_WRITE_ERROR;
        }
    }

    struct setup_record next;
    uint8_t *raw = (uint8_t *)&next;
    for (uint32_t i = 0; i < sizeof(next); i++) raw[i] = 0;
    next.magic = SETUP_MAGIC;
    next.version = SETUP_VERSION;
    next.installed = 1;
    next.theme = (uint8_t)theme;
    next.timezone = (uint8_t)timezone;
    next.edition = (uint8_t)edition;
    next.guest = guest ? 1 : 0;
    next.salt = guest ? 0 : setup_new_salt();
    next.hash = guest ? 0 : setup_hash_password(password, next.salt);
    setup_copy(next.username, username, SETUP_USERNAME_MAX);

    if (setup_store_record(&next) != 0) {
        console_write("[SETUP] could not write account record\n");
        return SETUP_INSTALL_WRITE_ERROR;
    }

    record = next;
    setup_loaded = true;
    rtc_set_timezone(timezone);
    setup_apply_edition();
    console_write(guest
        ? "[SETUP] guest session and installation edition saved\n"
        : "[SETUP] local account and preferences saved\n");
    return 0;
}

int setup_install(const char *username, const char *password,
                  int theme, int timezone, int edition) {
    return setup_write_account(username, password, theme, timezone,
                               edition, false);
}

int setup_install_guest(int theme, int timezone, int edition) {
    return setup_write_account("Guest", "", theme, timezone, edition, true);
}

void setup_apply_edition(void) {
    if (setup_loaded) {
        int pending = setup_pending_value();
        if (pending >= 0 && pending != record.edition) {
            int previous = record.edition;
            record.edition = (uint8_t)pending;
            if (setup_store_record(&record) != 0) {
                record.edition = (uint8_t)previous;
                console_write("[SETUP] could not persist edition change; "
                              "keeping current edition\n");
                return;
            }
            console_write("[SETUP] staged edition applied on boot\n");
        }
        setup_clear_pending();
    }

    int edition = setup_edition();

    /* The edition is a runtime contract, not just a label: leaving apps in
     * memory that the selected edition does not include would make Minimal
     * and Standard only cosmetic. */
    if (edition < SETUP_EDITION_EXTRA)
        spx_uninstall("Doom");
    if (edition == SETUP_EDITION_MINIMAL) {
        spx_uninstall("Paint");
        spx_uninstall("SOLPKG");
        spx_uninstall("Editor");
        spx_uninstall("Tetris");
        spx_uninstall("Blocks");
        spx_uninstall("Calculator");
        spx_uninstall("Browser");
    }

    /* Remove installer scratch records. Extra is allowed to be the cleanest
     * install: it removes the staging files as soon as the account is durable. */
    solfs_delete("settings/account.tmp");
    solfs_delete("settings/edition.tmp");
    solfs_delete("settings/install.tmp");
}

int setup_disk_status(void) {
    if (solfs_is_mounted()) return SETUP_DISK_READY;
    if (solfs_can_format()) return SETUP_DISK_BLANK;
    return SETUP_DISK_NONE;
}

int setup_set_theme(int theme) {
    if (!setup_loaded || !setup_theme_valid(theme)) return -1;
    struct setup_record next = record;
    next.theme = (uint8_t)theme;
    if (setup_store_record(&next) != 0) return -1;
    record = next;
    return 0;
}

int setup_change_password(const char *password) {
    if (!setup_loaded || !password) return -1;
    int length = setup_strlen(password);
    if (length < 4 || length >= SETUP_PASSWORD_MAX) return -1;

    struct setup_record next = record;
    next.salt = setup_new_salt();
    next.hash = setup_hash_password(password, next.salt);

    if (setup_store_record(&next) != 0) return -1;
    record = next;
    console_write("[SETUP] account password updated\n");
    return 0;
}

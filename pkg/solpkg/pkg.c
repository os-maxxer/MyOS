#include <solis/graphics.h>
#include <solis/gui.h>
#include <solis/spx.h>
#include <solis/solfs.h>
#include <solis/vfs.h>
#include <solis/net.h>
#include <stdbool.h>

#define LINE_H 16
#define COLS 60
#define LINE_BUF 256
#define MAX_REPO_PKGS 16
#define PKG_BUF_SIZE (1024 * 1024)

static char lines[32][COLS];
static int line_count;
static bool dirty;

static char line_buf[LINE_BUF];
static int line_pos;

/* One package buffer for disk installs and downloads. Packages are flat
 * images up to a full SPX slot, so a 64 KiB buffer silently rejected every
 * real app; the slot this manager runs in leaves room for 1 MiB. */
static uint8_t pkg_buf[PKG_BUF_SIZE];

struct repo_entry {
    char name[24];
    char version[12];
    char desc[44];
    char file[64];
    int  slot;
};

static struct repo_entry repo_pkgs[MAX_REPO_PKGS];
static int repo_pkg_count;
static bool repo_loaded;

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Package names differ between the manifest ("taskmanager"), the export
 * table ("Task Manager") and the file name. Compare loosely. */
static int name_equiv(const char *a, const char *b) {
    while (*a && *b) {
        char ca = lower(*a), cb = lower(*b);
        if (ca == ' ' || ca == '_' || ca == '-') { a++; continue; }
        if (cb == ' ' || cb == '_' || cb == '-') { b++; continue; }
        if (ca != cb) return 0;
        a++; b++;
    }
    while (*a == ' ' || *a == '_' || *a == '-') a++;
    while (*b == ' ' || *b == '_' || *b == '-') b++;
    return *a == *b;
}

static void clear_screen(void) {
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < COLS; j++)
            lines[i][j] = ' ';
    line_count = 0;
}

static void add_line(const char *s) {
    if (line_count >= 32) {
        for (int i = 0; i < 31; i++)
            for (int j = 0; j < COLS; j++)
                lines[i][j] = lines[i + 1][j];
        line_count = 31;
    }
    int i = 0;
    while (*s && i < COLS - 1)
        lines[line_count][i++] = *s++;
    while (i < COLS - 1)
        lines[line_count][i++] = ' ';
    lines[line_count][COLS - 1] = '\0';
    line_count++;
}

/* Small append-oriented string builder, so messages can mix literals and
 * numbers without a printf. */
struct sbuf { char *buf; int len; int max; };

static void sb_init(struct sbuf *b, char *buf, int max) {
    b->buf = buf; b->len = 0; b->max = max;
    if (max > 0) buf[0] = '\0';
}

static void sb_str(struct sbuf *b, const char *s) {
    while (*s && b->len < b->max - 1) b->buf[b->len++] = *s++;
    b->buf[b->len] = '\0';
}

static void sb_num(struct sbuf *b, uint32_t n) {
    char tmp[12];
    int ti = 0;
    if (n == 0) tmp[ti++] = '0';
    while (n > 0) { tmp[ti++] = (char)('0' + (n % 10)); n /= 10; }
    while (ti > 0 && b->len < b->max - 1) b->buf[b->len++] = tmp[--ti];
    b->buf[b->len] = '\0';
}

static void prompt(void) {
    add_line("solpk> ");
}

static uint8_t repo_ip[4] = {10, 0, 2, 2};
static uint16_t repo_port = 8000;
static char repo_host[64] = "";
static char repo_host_str[64] = "10.0.2.2";
static int repo_has_hostname = 0;
static char repo_prefix[64] = "";

static void ip_to_str(const uint8_t *ip, char *buf) {
    int bi = 0;
    for (int i = 0; i < 4; i++) {
        int n = ip[i];
        char tmp[4];
        int ti = 0;
        if (n == 0) { tmp[ti++] = '0'; }
        while (n > 0) { tmp[ti++] = (char)('0' + (n % 10)); n /= 10; }
        while (ti > 0) buf[bi++] = tmp[--ti];
        if (i < 3) buf[bi++] = '.';
    }
    buf[bi] = '\0';
}

static int repo_resolve(void) {
    if (repo_has_hostname) {
        uint8_t ip[4];
        if (net_dns_resolve(repo_host, ip) < 0)
            return -1;
        for (int i = 0; i < 4; i++) repo_ip[i] = ip[i];
    }
    ip_to_str(repo_ip, repo_host_str);
    return 0;
}

static int parse_ip(const char *s, uint8_t *ip) {
    int octet = 0, part = 0;
    for (int i = 0; i < 4; i++) {
        octet = 0; part = 0;
        while (*s >= '0' && *s <= '9') {
            octet = octet * 10 + (*s - '0');
            s++; part++;
        }
        if (part == 0 || octet > 255) return -1;
        ip[i] = (uint8_t)octet;
        if (i < 3 && *s != '.') return -1;
        if (i < 3) s++;
    }
    return 0;
}

static void print_help(void) {
    add_line("SOLPKG 1.1 - Solis Package Manager");
    add_line("");
    add_line("Packages:");
    add_line("  search [term]       installed + repo packages");
    add_line("  install <file.spx>  install a package from disk");
    add_line("  local-list          list .spx files on disk");
    add_line("  local-install <n>   install <n>.spx from disk");
    add_line("  purge <name>        uninstall a package");
    add_line("  upgrade             reinstall every installed pkg");
    add_line("");
    add_line("Repository:");
    add_line("  repo add <url>      set repo, e.g. http://10.0.2.2:8000");
    add_line("  repo-list           show packages in the repo");
    add_line("  fetch <name>        download and install a package");
    add_line("  launch <name>       start an installed package");
    add_line("");
    add_line("  clear               clear the screen");
    add_line("  help                show this help");
    add_line("");
}

/* ------------------------------------------------------------------ */
/* Minimal JSON reading                                                */
/* ------------------------------------------------------------------ */

static const char *json_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* Find "key" at object depth 1 inside [obj, end) and return the pointer to
 * the value that follows the colon. Strings are skipped so a description
 * containing a key name cannot confuse the search. */
static const char *json_value(const char *obj, const char *end, const char *key) {
    const char *p = obj;
    int depth = 0;
    int key_len = 0;
    while (key[key_len]) key_len++;
    while (p < end) {
        char c = *p;
        if (c == '"') {
            const char *s = ++p;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) p++;
                p++;
            }
            if (p >= end) return 0;
            int len = (int)(p - s);
            p++;
            if (depth == 1 && len == key_len) {
                int match = 1;
                for (int i = 0; i < len; i++)
                    if (s[i] != key[i]) { match = 0; break; }
                if (match) {
                    const char *v = json_skip_ws(p);
                    if (v < end && *v == ':') return json_skip_ws(v + 1);
                }
            }
            continue;
        }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth <= 0) return 0;
        }
        p++;
    }
    return 0;
}

static int json_str(const char *obj, const char *end, const char *key,
                    char *out, int max) {
    const char *p = json_value(obj, end, key);
    if (!p || p >= end || *p != '"') return 0;
    p++;
    int n = 0;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            c = *p++;
            if (c == 'n' || c == 'r' || c == 't') c = ' ';
        }
        if (n < max - 1) out[n++] = c;
    }
    out[n] = '\0';
    return n;
}

static int json_int(const char *obj, const char *end, const char *key, int def) {
    const char *p = json_value(obj, end, key);
    if (!p || p >= end) return def;
    int value = 0, digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++; digits++;
    }
    return digits ? value : def;
}

/* Fill repo_pkgs from a repo.json body. Returns the number of entries. */
static int parse_manifest(const char *json, int len) {
    repo_pkg_count = 0;
    repo_loaded = false;
    const char *end = json + len;
    const char *p = json_value(json, end, "packages");
    if (!p || p >= end || *p != '[') return 0;
    p++;
    while (p < end && repo_pkg_count < MAX_REPO_PKGS) {
        p = json_skip_ws(p);
        if (p >= end || *p == ']') break;
        if (*p != '{') { p++; continue; }
        const char *obj = p;
        const char *q = p + 1;
        int depth = 1;
        while (q < end && depth > 0) {
            if (*q == '"') {
                q++;
                while (q < end && *q != '"') {
                    if (*q == '\\' && q + 1 < end) q++;
                    q++;
                }
                if (q < end) q++;
                continue;
            }
            if (*q == '{') depth++;
            else if (*q == '}') depth--;
            q++;
        }
        if (depth != 0) break;
        struct repo_entry *e = &repo_pkgs[repo_pkg_count];
        if (!json_str(obj, q, "name", e->name, sizeof(e->name))) {
            p = q;
            continue;
        }
        if (!json_str(obj, q, "version", e->version, sizeof(e->version)))
            e->version[0] = '\0';
        if (!json_str(obj, q, "description", e->desc, sizeof(e->desc)))
            e->desc[0] = '\0';
        if (!json_str(obj, q, "file", e->file, sizeof(e->file))) {
            /* The manifest may omit the path; default to <name>.spx. */
            int n = 0;
            while (e->name[n] && n < (int)sizeof(e->file) - 5) {
                e->file[n] = e->name[n];
                n++;
            }
            e->file[n++] = '.'; e->file[n++] = 's'; e->file[n++] = 'p';
            e->file[n++] = 'x'; e->file[n] = '\0';
        }
        e->slot = json_int(obj, q, "slot", -1);
        repo_pkg_count++;
        p = q;
        p = json_skip_ws(p);
        if (p < end && *p == ',') p++;
    }
    repo_loaded = repo_pkg_count > 0;
    return repo_pkg_count;
}

static int fetch_manifest(void) {
    if (!net_available()) {
        add_line("Network not available.");
        return -1;
    }
    if (repo_resolve() < 0) {
        add_line("Failed to resolve the repo hostname.");
        return -1;
    }
    char path[96];
    int pi = 0;
    if (repo_prefix[0]) {
        path[pi++] = '/';
        for (int i = 0; repo_prefix[i] && pi < 80; i++) path[pi++] = repo_prefix[i];
    }
    const char *j = "/repo.json";
    while (*j && pi < 90) path[pi++] = *j++;
    path[pi] = '\0';

    int n = net_http_get(repo_ip, repo_port, repo_host_str, path,
                         pkg_buf, 65535);
    if (n <= 0) return -1;
    pkg_buf[n] = '\0';
    return parse_manifest((const char *)pkg_buf, n);
}

static struct repo_entry *find_repo_pkg(const char *name) {
    for (int i = 0; i < repo_pkg_count; i++)
        if (name_equiv(repo_pkgs[i].name, name)) return &repo_pkgs[i];
    return 0;
}

/* ------------------------------------------------------------------ */
/* Install helpers                                                     */
/* ------------------------------------------------------------------ */

static void report_install_error(int rv, const char *what, int slot) {
    char msg[64];
    struct sbuf b;
    sb_init(&b, msg, sizeof(msg));
    sb_str(&b, what);
    if (rv == SPX_ERR_BUSY) {
        if (slot < 0) {
            sb_str(&b, ": no free slot.");
            add_line(msg);
            add_line("Purge an app to make room.");
            return;
        }
        sb_str(&b, ": slot ");
        sb_num(&b, (uint32_t)slot);
        sb_str(&b, " is occupied.");
        add_line(msg);
        if (slot >= 0) {
            const struct app_exports *e = spx_get_exports(slot);
            if (e && e->name) {
                char owner[64];
                struct sbuf o;
                sb_init(&o, owner, sizeof(owner));
                sb_str(&o, "Running there: ");
                sb_str(&o, e->name);
                add_line(owner);
            }
        }
        add_line("Purge that app first, then retry.");
    } else if (rv == SPX_ERR_BASE) {
        sb_str(&b, ": not linked for any SPX slot.");
        add_line(msg);
    } else if (rv == SPX_ERR_BAD) {
        sb_str(&b, ": malformed or too large.");
        add_line(msg);
    } else {
        sb_str(&b, ": no free slot for an unpinned package.");
        add_line(msg);
        add_line("Purge an app to make room.");
    }
}

static int install_buffer(uint32_t size, const char *label) {
    if (size < sizeof(struct spx_header)) {
        add_line("Not an SPX package (too small).");
        return -1;
    }
    const struct spx_header *hdr = (const struct spx_header *)pkg_buf;
    if (hdr->magic != SPX_MAGIC) {
        add_line("Not an SPX package (bad magic).");
        return -1;
    }

    /* Never overwrite the slot this manager is executing from. */
    int self = spx_find_slot("SOLPKG");
    int target = -1;
    if (hdr->version >= 3 && hdr->load_base)
        target = spx_slot_for_base(hdr->load_base);
    if (self >= 0 && target == self) {
        add_line("Refusing to replace the running SOLPKG.");
        return -1;
    }
    /* Replacing bytes under a live window would fault the running app; the
     * user can close it and retry. */
    if (target >= 0 && gui_app_window(target) >= 0) {
        const struct app_exports *e = spx_get_exports(target);
        if (e && e->name) {
            char msg[64];
            struct sbuf b;
            sb_init(&b, msg, sizeof(msg));
            sb_str(&b, "Close ");
            sb_str(&b, e->name);
            sb_str(&b, " before installing.");
            add_line(msg);
        } else {
            add_line("Close the app in that slot first.");
        }
        return -1;
    }

    add_line("Installing...");
    int slot = spx_install(pkg_buf, size);
    if (slot < 0) {
        report_install_error(slot, "Install failed", target);
        return -1;
    }

    char msg[64];
    struct sbuf b;
    sb_init(&b, msg, sizeof(msg));
    sb_str(&b, "Installed ");
    sb_str(&b, label ? label : "package");
    sb_str(&b, " in slot ");
    sb_num(&b, (uint32_t)slot);
    add_line(msg);
    add_line("Start it with: launch <name>");
    return slot;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static void cmd_local_list(void) {
    char names[32][24];
    int count = vfs_ls(names, 32);
    int shown = 0;
    add_line("--- Local packages (.spx) ---");
    for (int i = 0; i < count; i++) {
        int n = 0;
        while (names[i][n]) n++;
        if (n < 4) continue;
        if (!(names[i][n - 4] == '.' &&
              (names[i][n - 3] == 's' || names[i][n - 3] == 'S') &&
              (names[i][n - 2] == 'p' || names[i][n - 2] == 'P') &&
              (names[i][n - 1] == 'x' || names[i][n - 1] == 'X')))
            continue;
        add_line(names[i]);
        shown++;
    }
    if (shown == 0)
        add_line("No .spx files here. Use 'local-install <name>'.");
    add_line("--- End ---");
}

/* Returns 1 when the file existed (whether or not the install succeeded)
 * and 0 when the path could not be opened, so callers can try a fallback. */
static int read_package_file(const char *path, const char *label) {
    int fd = vfs_open(path);
    if (fd < 0) return 0;
    int size = vfs_get_size(fd);
    if (size <= 0 || size > PKG_BUF_SIZE) {
        add_line("Invalid package size (max 1 MiB).");
        return 1;
    }
    int n = vfs_read(fd, pkg_buf, (uint32_t)size);
    if (n <= 0) {
        add_line("Failed to read the package.");
        return 1;
    }
    install_buffer((uint32_t)n, label);
    return 1;
}

static void cmd_local_install(const char *name) {
    if (!name || !*name) {
        add_line("Usage: local-install <name>");
        return;
    }
    char path[64];
    int pi = 0;
    path[pi++] = '/';
    const char *n = name;
    while (*n && pi < 40) path[pi++] = *n++;
    path[pi++] = '.'; path[pi++] = 's'; path[pi++] = 'p'; path[pi++] = 'x';
    path[pi] = '\0';
    if (read_package_file(path, name)) return;

    /* Also accept a package directory: /<name>/<name>.spx */
    pi = 0;
    path[pi++] = '/';
    n = name;
    while (*n && pi < 24) path[pi++] = *n++;
    path[pi++] = '/';
    n = name;
    while (*n && pi < 40) path[pi++] = *n++;
    path[pi++] = '.'; path[pi++] = 's'; path[pi++] = 'p'; path[pi++] = 'x';
    path[pi] = '\0';
    if (read_package_file(path, name)) return;

    add_line("Package file not found on disk.");
}

static void cmd_repo_add(const char *url) {
    if (!url || !*url) {
        add_line("Usage: repo add http://hostname:port[/prefix]");
        return;
    }

    while (*url == ' ') url++;
    if (url[0] == 'a' && url[1] == 'd' && url[2] == 'd' && url[3] == ' ')
        url += 4;

    if (url[0] != 'h' || url[1] != 't' || url[2] != 't' || url[3] != 'p' ||
        url[4] != ':' || url[5] != '/' || url[6] != '/') {
        add_line("Only http:// URLs are supported.");
        return;
    }
    url += 7;

    int hi = 0;
    const char *p = url;
    while (*p && *p != ':' && *p != '/' && hi < 63)
        repo_host[hi++] = *p++;
    repo_host[hi] = '\0';

    int port = 80;
    if (*p == ':') {
        p++;
        port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port <= 0 || port > 65535) {
            add_line("Invalid port.");
            return;
        }
    }
    repo_port = (uint16_t)port;

    repo_prefix[0] = '\0';
    if (*p == '/') {
        p++;
        int pi = 0;
        while (*p && pi < 63) repo_prefix[pi++] = *p++;
        /* Drop a trailing slash so path joins stay predictable. */
        while (pi > 0 && repo_prefix[pi - 1] == '/') pi--;
        repo_prefix[pi] = '\0';
    }

    if (parse_ip(repo_host, repo_ip) == 0) {
        repo_has_hostname = 0;
    } else {
        repo_has_hostname = 1;
        if (repo_resolve() < 0) {
            add_line("Failed to resolve hostname.");
            return;
        }
    }
    repo_loaded = false;
    repo_pkg_count = 0;

    char msg[64];
    struct sbuf b;
    sb_init(&b, msg, sizeof(msg));
    sb_str(&b, "Repo: ");
    sb_str(&b, repo_host);
    sb_str(&b, ":");
    sb_num(&b, repo_port);
    if (repo_prefix[0]) { sb_str(&b, "/"); sb_str(&b, repo_prefix); }
    add_line(msg);
}

static void cmd_repo_list(const char *arg) {
    (void)arg;
    add_line("Fetching repo index...");
    int count = fetch_manifest();
    if (count <= 0) {
        add_line("Could not read repo.json.");
        add_line("Set one with: repo add http://10.0.2.2:8000");
        return;
    }
    add_line("--- Available packages ---");
    for (int i = 0; i < count; i++) {
        struct repo_entry *e = &repo_pkgs[i];
        char line[COLS];
        struct sbuf b;
        sb_init(&b, line, sizeof(line));
        sb_str(&b, e->name[0] ? e->name : "?");
        if (e->version[0]) { sb_str(&b, " "); sb_str(&b, e->version); }
        add_line(line);
        if (e->desc[0]) {
            char desc[COLS];
            struct sbuf d;
            sb_init(&d, desc, sizeof(desc));
            sb_str(&d, "  ");
            sb_str(&d, e->desc);
            add_line(desc);
        }
    }
    add_line("--- End ---");
}

static void cmd_search(const char *term) {
    char names[SPX_MAX_APPS][SPX_NAME_LEN];
    int count = spx_list_installed(names, SPX_MAX_APPS);
    while (*term == ' ') term++;

    add_line("--- Installed ---");
    int shown = 0;
    for (int i = 0; i < count; i++) {
        int slot = spx_find_slot(names[i]);
        char line[COLS];
        struct sbuf b;
        sb_init(&b, line, sizeof(line));
        sb_str(&b, "  ");
        sb_str(&b, names[i]);
        sb_str(&b, " (slot ");
        sb_num(&b, (uint32_t)slot);
        sb_str(&b, ")");
        if (*term && !name_equiv(names[i], term) && *term) {
            /* Substring filter, case-insensitive. */
            int found = 0;
            for (int a = 0; names[i][a] && !found; a++) {
                int k = 0;
                while (term[k] && names[i][a + k] &&
                       lower(names[i][a + k]) == lower(term[k])) k++;
                if (!term[k]) found = 1;
            }
            if (!found) continue;
        }
        add_line(line);
        shown++;
    }
    if (!shown) add_line("  (none)");

    if (!repo_loaded) {
        add_line("--- Repo (run repo-list first) ---");
        return;
    }
    add_line("--- Repository ---");
    shown = 0;
    for (int i = 0; i < repo_pkg_count; i++) {
        struct repo_entry *e = &repo_pkgs[i];
        if (*term) {
            int found = 0;
            for (int a = 0; e->name[a] && !found; a++) {
                int k = 0;
                while (term[k] && e->name[a + k] &&
                       lower(e->name[a + k]) == lower(term[k])) k++;
                if (!term[k]) found = 1;
            }
            if (!found) continue;
        }
        char line[COLS];
        struct sbuf b;
        sb_init(&b, line, sizeof(line));
        sb_str(&b, "  ");
        sb_str(&b, e->name);
        sb_str(&b, " ");
        sb_str(&b, e->version);
        add_line(line);
        shown++;
    }
    if (!shown) add_line("  (no match)");
}

static void cmd_fetch(const char *name) {
    if (!name || !*name) {
        add_line("Usage: fetch <name>");
        return;
    }
    while (*name == ' ') name++;
    if (!net_available()) {
        add_line("Network not available.");
        return;
    }
    if (!repo_loaded && fetch_manifest() <= 0) {
        add_line("Could not read repo.json.");
        return;
    }
    struct repo_entry *e = find_repo_pkg(name);
    if (!e) {
        add_line("No such package in the repo.");
        add_line("Run 'repo-list' to see what is available.");
        return;
    }
    if (repo_resolve() < 0) {
        add_line("Failed to resolve the repo hostname.");
        return;
    }

    char path[128];
    int pi = 0;
    if (repo_prefix[0]) {
        path[pi++] = '/';
        for (int i = 0; repo_prefix[i] && pi < 60; i++) path[pi++] = repo_prefix[i];
    }
    path[pi++] = '/';
    for (int i = 0; e->file[i] && pi < 120; i++) {
        path[pi++] = (e->file[i] == '\\') ? '/' : e->file[i];
    }
    path[pi] = '\0';

    char msg[64];
    struct sbuf b;
    sb_init(&b, msg, sizeof(msg));
    sb_str(&b, "Downloading ");
    sb_str(&b, e->name);
    add_line(msg);

    int n = net_http_get(repo_ip, repo_port, repo_host_str, path,
                         pkg_buf, PKG_BUF_SIZE - 1);
    if (n <= 0) {
        add_line("Download failed.");
        add_line("Check that the repo serves this file.");
        return;
    }
    if (n >= PKG_BUF_SIZE - 1) {
        add_line("Download too large (1 MiB limit).");
        return;
    }
    install_buffer((uint32_t)n, e->name);
}

static void cmd_upgrade(const char *arg) {
    (void)arg;
    if (!repo_loaded && fetch_manifest() <= 0) {
        add_line("Could not read repo.json.");
        return;
    }
    int done = 0;
    for (int i = 0; i < repo_pkg_count; i++) {
        struct repo_entry *e = &repo_pkgs[i];
        if (spx_find_slot(e->name) < 0) continue;
        if (e->slot >= 0 && spx_get_exports(e->slot) == 0) continue;
        cmd_fetch(e->name);
        done++;
    }
    if (!done) add_line("Nothing installed from the repo to upgrade.");
}

static void cmd_install(const char *path) {
    if (!path || !*path) {
        add_line("Usage: install <filename.spx>");
        return;
    }
    add_line("Reading package from disk...");
    if (!read_package_file(path, path))
        add_line("Error: file not found or unreadable.");
}

static void cmd_purge(const char *name) {
    if (!name || !*name) {
        add_line("Usage: purge <appname>");
        return;
    }
    int self = spx_find_slot("SOLPKG");
    int slot = spx_find_slot(name);
    if (slot >= 0 && slot == self) {
        add_line("Refusing to uninstall the running SOLPKG.");
        return;
    }
    if (spx_uninstall(name) < 0) {
        add_line("Error: package not found.");
        return;
    }
    char msg[64];
    struct sbuf b;
    sb_init(&b, msg, sizeof(msg));
    sb_str(&b, "Uninstalled: ");
    sb_str(&b, name);
    add_line(msg);
}

static void cmd_launch(const char *arg) {
    while (*arg == ' ') arg++;
    if (!*arg) {
        add_line("Usage: launch <name>");
        return;
    }

    char wanted[SPX_NAME_LEN];
    int n = 0;
    while (arg[n] && arg[n] != ' ' && n < SPX_NAME_LEN - 1) {
        wanted[n] = lower(arg[n]);
        n++;
    }
    wanted[n] = (char)0;

    char names[SPX_MAX_APPS][SPX_NAME_LEN];
    int count = spx_list_installed(names, SPX_MAX_APPS);
    for (int i = 0; i < count; i++) {
        int k = 0;
        while (wanted[k] && names[i][k] && lower(names[i][k]) == wanted[k]) k++;
        if (wanted[k] || names[i][k]) continue;
        int slot = spx_find_slot(names[i]);
        if (slot < 0 || gui_launch_app(slot) < 0) {
            add_line("Could not open a window for it.");
            return;
        }
        char msg[64];
        struct sbuf b;
        sb_init(&b, msg, sizeof(msg));
        sb_str(&b, "Launched ");
        sb_str(&b, names[i]);
        add_line(msg);
        return;
    }
    add_line("Not installed. Try: search, then fetch <name>.");
}

static void cmd_execute(const char *cmd) {
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    char command[32];
    int ci = 0;
    while (*cmd && *cmd != ' ' && ci < 31) {
        command[ci++] = *cmd;
        cmd++;
    }
    command[ci] = '\0';
    while (*cmd == ' ') cmd++;

    if (str_eq(command, "help")) {
        print_help();
    } else if (str_eq(command, "clear")) {
        clear_screen();
    } else if (str_eq(command, "search")) {
        cmd_search(cmd);
    } else if (str_eq(command, "install")) {
        cmd_install(cmd);
    } else if (str_eq(command, "purge")) {
        cmd_purge(cmd);
    } else if (str_eq(command, "local-list")) {
        cmd_local_list();
    } else if (str_eq(command, "local-install")) {
        cmd_local_install(cmd);
    } else if (str_eq(command, "repo")) {
        cmd_repo_add(cmd);
    } else if (str_eq(command, "repo-list")) {
        cmd_repo_list(cmd);
    } else if (str_eq(command, "launch")) {
        cmd_launch(cmd);
    } else if (str_eq(command, "fetch")) {
        cmd_fetch(cmd);
    } else if (str_eq(command, "upgrade")) {
        cmd_upgrade(cmd);
    } else {
        char msg[48];
        struct sbuf b;
        sb_init(&b, msg, sizeof(msg));
        sb_str(&b, "Unknown command: ");
        sb_str(&b, command);
        add_line(msg);
        add_line("Type 'help' for available commands.");
    }
}

void pkg_init(void) {
    clear_screen();
    print_help();
    prompt();
    line_pos = 0;
    dirty = true;
}

void pkg_draw(int x, int y, int w, int h) {
    if (!dirty) return;
    dirty = false;

    int rows = (h - 8) / LINE_H;
    if (rows > line_count) rows = line_count;

    graphics_fill_rect(x, y, w, h, 0xFF1E1E2E);

    for (int r = 0; r < rows; r++) {
        graphics_draw_string(x + 4, y + 4 + r * LINE_H, lines[r], 0xFF00FF00);
    }

    char prompt_line[COLS];
    int bi = 0;
    const char *p = "solpk> ";
    while (*p) prompt_line[bi++] = *p++;
    for (int i = 0; i < line_pos; i++)
        prompt_line[bi++] = line_buf[i];
    prompt_line[bi] = '\0';
    graphics_draw_string(x + 4, y + 4 + (line_count < rows ? line_count : rows) * LINE_H, prompt_line, 0xFF00FF00);
}

void pkg_handle_key(char key) {
    if (key == '\n') {
        line_buf[line_pos] = '\0';
        cmd_execute(line_buf);
        prompt();
        line_pos = 0;
    } else if (key == '\b') {
        if (line_pos > 0) line_pos--;
    } else if (key == 0x1b) {
        line_pos = 0;
    } else if (key >= 32) {
        if (line_pos < LINE_BUF - 1)
            line_buf[line_pos++] = key;
    }
    dirty = true;
}

#include <nyx/graphics.h>
#include <nyx/npx.h>
#include <nyx/nofs.h>
#include <nyx/vfs.h>
#include <nyx/net.h>
#include <stdbool.h>

#define LINE_H 16
#define COLS 60
#define LINE_BUF 256

static char lines[32][COLS];
static int line_count;
static bool dirty;

static char line_buf[LINE_BUF];
static int line_pos;

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void clear_screen(void) {
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < COLS; j++)
            lines[i][j] = ' ';
    line_count = 0;
}

static void add_line(const char *s) {
    if (line_count >= 32) return;
    int i = 0;
    while (*s && i < COLS - 1)
        lines[line_count][i++] = *s++;
    while (i < COLS - 1)
        lines[line_count][i++] = ' ';
    lines[line_count][COLS - 1] = '\0';
    line_count++;
}

static void uint32_to_str(uint32_t n, char *buf) {
    char tmp[12];
    int ti = 0;
    if (n == 0) { tmp[ti++] = '0'; }
    while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
    int bi = 0;
    while (ti > 0) buf[bi++] = tmp[--ti];
    buf[bi] = '\0';
}

static void prompt(void) {
    add_line("npxpk> ");
}

static uint8_t repo_ip[4] = {10, 0, 2, 2};
static uint16_t repo_port = 8000;
static char repo_host[64] = "";
static char repo_host_str[64] = "10.0.2.2";
static int repo_has_hostname = 0;

static void ip_to_str(const uint8_t *ip, char *buf) {
    int bi = 0;
    for (int i = 0; i < 4; i++) {
        int n = ip[i];
        char tmp[4];
        int ti = 0;
        if (n == 0) { tmp[ti++] = '0'; }
        while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
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
    add_line("NYPKG v1.0 - Nyx Package Manager");
    add_line("");
    add_line("Commands:");
    add_line("  help                  - show this help");
    add_line("  search [term]         - list installed packages");
    add_line("  install <file>        - install a .npx package from disk");
    add_line("  purge <name>          - uninstall a package by name");
    add_line("  local-list            - list packages on local disk");
    add_line("  local-install <name>  - install a package from local disk");
    add_line("  repo add <url>        - set repo (e.g. http://10.0.2.2:8000)");
    add_line("  repo-list             - list packages from repo");
    add_line("  fetch <name>          - download and install from repo");
    add_line("  clear                 - clear screen");
    add_line("");
}

static void cmd_local_list(const char *arg) {
    (void)arg;
    int fd = vfs_open("/repo.json");
    if (fd < 0) {
        fd = vfs_open("repo.json");
        if (fd < 0) {
            add_line("No local repo found on disk.");
            return;
        }
    }
    int size = vfs_get_size(fd);
    if (size <= 0) {
        add_line("Local repo is empty.");
        return;
    }
    uint8_t buf[2048];
    int n = vfs_read(fd, buf, 2048);
    if (n <= 0) {
        add_line("Failed to read local repo.");
        return;
    }
    buf[n] = '\0';
    add_line("--- Local Packages ---");
    char *line = (char *)buf;
    while (*line) {
        char lbuf[60];
        int bi = 0;
        while (*line && *line != '\n' && bi < 58) lbuf[bi++] = *line++;
        if (*line == '\n') line++;
        lbuf[bi] = '\0';
        if (bi > 0) add_line(lbuf);
    }
    add_line("--- End ---");
}

static void cmd_local_install(const char *name) {
    if (!name || !*name) {
        add_line("Usage: local-install <name>");
        return;
    }
    char fname[32];
    int fi = 0;
    const char *n = name;
    while (*n && fi < 27) fname[fi++] = *n++;
    fname[fi++] = '.'; fname[fi++] = 'n'; fname[fi++] = 'p'; fname[fi++] = 'x';
    fname[fi] = '\0';

    char fpath[64];
    fi = 0;
    fpath[fi++] = '/';
    n = name;
    while (*n && fi < 62) fpath[fi++] = *n++;
    fpath[fi++] = '.'; fpath[fi++] = 'n'; fpath[fi++] = 'p'; fpath[fi++] = 'x';
    fpath[fi] = '\0';

    int fd = vfs_open(fpath);
    if (fd < 0) {
        fd = vfs_open(fname);
        if (fd < 0) {
            add_line("Package file not found on disk.");
            return;
        }
    }
    int size = vfs_get_size(fd);
    if (size <= 0 || size > 65536) {
        add_line("Invalid package size.");
        return;
    }
    uint8_t buf[65536];
    int nread = vfs_read(fd, buf, (uint32_t)size);
    if (nread <= 0) {
        add_line("Failed to read package.");
        return;
    }
    add_line("Installing...");
    int slot = npx_install(buf, (uint32_t)nread);
    if (slot < 0) {
        add_line("Install failed (no free slots or bad package).");
        return;
    }
    {
        char msg[64];
        int bi = 0;
        const char *p = "Installed from disk: ";
        while (*p) msg[bi++] = *p++;
        p = name;
        while (*p && bi < 56) msg[bi++] = *p++;
        msg[bi] = '\0';
        add_line(msg);
    }
}

static void cmd_repo_add(const char *url) {
    if (!url || !*url) {
        add_line("Usage: repo add http://hostname:port");
        return;
    }

    while (*url == ' ') url++;
    if (url[0] == 'a' && url[1] == 'd' && url[2] == 'd' && url[3] == ' ')
        url += 4;

    if (url[0] != 'h' || url[1] != 't' || url[2] != 't' || url[3] != 'p' ||
        url[4] != ':' || url[5] != '/' || url[6] != '/') {
        add_line("Only http:// URLs supported.");
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

    if (parse_ip(repo_host, repo_ip) == 0) {
        repo_has_hostname = 0;
    } else {
        repo_has_hostname = 1;
        if (repo_resolve() < 0) {
            add_line("Failed to resolve hostname.");
            return;
        }
    }

    {
        char msg[64];
        int bi = 0;
        const char *s = "Repo: ";
        while (*s) msg[bi++] = *s++;
        s = repo_host;
        while (*s && bi < 50) msg[bi++] = *s++;
        msg[bi++] = ':';
        char pt[8];
        uint32_to_str(repo_port, pt);
        s = pt;
        while (*s && bi < 62) msg[bi++] = *s++;
        msg[bi] = '\0';
        add_line(msg);
    }
}

static void cmd_repo_list(const char *arg) {
    (void)arg;
    if (!net_available()) {
        add_line("Network not available.");
        return;
    }
    add_line("Fetching repo list...");
    if (repo_resolve() < 0) {
        add_line("Failed to resolve repo hostname.");
        return;
    }
    uint8_t resp[4096];
    int n = net_http_get(repo_ip, repo_port, repo_host_str, "/repo.json", resp, 4096);
    if (n <= 0) {
        add_line("Connection failed. Set repo with: repo add http://host:port");
        return;
    }
    resp[n] = '\0';
    add_line("--- Available Packages ---");
    char *line = (char *)resp;
    while (*line) {
        char buf[60];
        int bi = 0;
        while (*line && *line != '\n' && bi < 58) buf[bi++] = *line++;
        if (*line == '\n') line++;
        buf[bi] = '\0';
        if (bi > 0) add_line(buf);
    }
    add_line("--- End ---");
}

static void cmd_fetch(const char *name) {
    if (!name || !*name) {
        add_line("Usage: fetch <name>");
        return;
    }
    if (!net_available()) {
        add_line("Network not available.");
        return;
    }

    if (repo_resolve() < 0) {
        add_line("Failed to resolve repo hostname.");
        return;
    }

    char path[128];
    int pi = 0;
    path[pi++] = '/';
    const char *n = name;
    while (*n && pi < 110) path[pi++] = *n++;
    path[pi++] = '/';
    n = name;
    while (*n && pi < 120) path[pi++] = *n++;
    path[pi++] = '.'; path[pi++] = 'n'; path[pi++] = 'p'; path[pi++] = 'x';
    path[pi] = '\0';

    {
        char msg[64];
        int bi = 0;
        const char *p = "Downloading ";
        while (*p) msg[bi++] = *p++;
        p = name;
        while (*p && bi < 56) msg[bi++] = *p++;
        msg[bi] = '\0';
        add_line(msg);
    }

    uint8_t resp[16384];
    int n_bytes = net_http_get(repo_ip, repo_port, repo_host_str, path, resp, 16384);
    if (n_bytes <= 0) {
        add_line("Download failed.");
        return;
    }

    add_line("Installing...");
    int slot = npx_install(resp, (uint32_t)n_bytes);
    if (slot < 0) {
        add_line("Install failed (no free slots or bad package).");
        return;
    }

    {
        char msg[64];
        int bi = 0;
        const char *p = "Installed ";
        while (*p) msg[bi++] = *p++;
        p = name;
        while (*p && bi < 56) msg[bi++] = *p++;
        msg[bi] = '\0';
        add_line(msg);
    }
}

static void cmd_search(const char *arg) {
    (void)arg;
    char names[NPX_MAX_APPS][NPX_NAME_LEN];
    int count = npx_list_installed(names, NPX_MAX_APPS);

    if (count == 0) {
        add_line("No packages installed.");
        return;
    }

    char buf[64];
    int bi;

    add_line("Installed packages:");
    for (int i = 0; i < count; i++) {
        int slot = npx_find_slot(names[i]);
        bi = 0;
        buf[bi++] = ' ';
        buf[bi++] = ' ';
        const char *s = names[i];
        while (*s && bi < 40) buf[bi++] = *s++;
        const char *p = " (slot ";
        while (*p && bi < 60) buf[bi++] = *p++;
        char slot_str[4];
        uint32_to_str((uint32_t)slot, slot_str);
        const char *q = slot_str;
        while (*q && bi < 60) buf[bi++] = *q++;
        buf[bi++] = ')';
        buf[bi] = '\0';
        add_line(buf);
    }

    if (count == 1)
        add_line("1 package total.");
    else {
        char total[32];
        uint32_to_str((uint32_t)count, total);
        bi = 0;
        const char *p = total;
        while (*p) buf[bi++] = *p++;
        const char *q = " packages total.";
        while (*q && bi < 60) buf[bi++] = *q++;
        buf[bi] = '\0';
        add_line(buf);
    }
}

static void cmd_install(const char *path) {
    if (!path || !*path) {
        add_line("Usage: install <filename>");
        return;
    }

    add_line("Reading package from disk...");

    int fd = vfs_open(path);
    if (fd < 0) {
        add_line("Error: file not found.");
        return;
    }

    int size = vfs_get_size(fd);
    if (size <= 0 || size > 65536) {
        add_line("Error: invalid or too large.");
        return;
    }

    uint8_t buf[65536];
    int n = vfs_read(fd, buf, (uint32_t)size);
    if (n <= 0) {
        add_line("Error: could not read file.");
        return;
    }

    add_line("Installing...");
    int slot = npx_install(buf, (uint32_t)n);
    if (slot < 0) {
        add_line("Error: install failed (no free slots or bad package).");
        return;
    }

    add_line("Package installed successfully!");

    char names[NPX_MAX_APPS][NPX_NAME_LEN];
    int count = npx_list_installed(names, NPX_MAX_APPS);
    char msg[64];
    int bi = 0;
    const char *p = "Installed in slot ";
    while (*p) msg[bi++] = *p++;
    char slot_str[4];
    uint32_to_str((uint32_t)slot, slot_str);
    p = slot_str;
    while (*p) msg[bi++] = *p++;
    const char *q = " (";
    while (*q) msg[bi++] = *q++;
    uint32_to_str((uint32_t)count, slot_str);
    q = slot_str;
    while (*q) msg[bi++] = *q++;
    q = " total)";
    while (*q) msg[bi++] = *q++;
    msg[bi] = '\0';
    add_line(msg);
}

static void cmd_purge(const char *name) {
    if (!name || !*name) {
        add_line("Usage: purge <appname>");
        return;
    }

    int result = npx_uninstall(name);
    if (result < 0) {
        add_line("Error: package not found.");
        return;
    }

    char msg[64];
    int bi = 0;
    const char *p = "Uninstalled: ";
    while (*p) msg[bi++] = *p++;
    p = name;
    while (*p && bi < 60) msg[bi++] = *p++;
    msg[bi] = '\0';
    add_line(msg);
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
        cmd_local_list(cmd);
    } else if (str_eq(command, "local-install")) {
        cmd_local_install(cmd);
    } else if (str_eq(command, "repo")) {
        cmd_repo_add(cmd);
    } else if (str_eq(command, "repo-list")) {
        cmd_repo_list(cmd);
    } else if (str_eq(command, "fetch")) {
        cmd_fetch(cmd);
    } else {
        char msg[48];
        int bi = 0;
        const char *p = "Unknown command: ";
        while (*p) msg[bi++] = *p++;
        p = command;
        while (*p && bi < 46) msg[bi++] = *p++;
        msg[bi] = '\0';
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
    const char *p = "npxpk> ";
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

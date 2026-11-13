#include <solis/apps/terminal.h>
#include <solis/apps/ccompiler.h>
#include <solis/apps/lua.h>
#include <solis/jit.h>
#include <solis/dbg.h>
#include <solis/graphics.h>
#include <solis/vfs.h>
#include <solis/solfs.h>
#include <solis/setup.h>
#include <solis/timer.h>
#include <solis/ports.h>
#include <solis/spx.h>
#include <stdbool.h>

extern int gui_launch_app(int slot);
extern int net_ping(const uint8_t *ip, uint32_t timeout_ms);
extern int net_arp_resolve(const uint8_t *ip, uint8_t *mac);
extern int net_available(void);
extern int dbg_read(char *buf, int max);
extern void dbg_get_health(struct dbg_health *out);

#define TERM_ROWS 32
#define TERM_COLS 80
#define TERM_BUF (TERM_ROWS * TERM_COLS)
#define LINE_BUF 256

#define TERM_COL_TEXT   0xEAEAEA
#define TERM_COL_ACCENT 0xD9A441
#define TERM_COL_BG     0xFF121820
#define TERM_COL_OK     0xFF5CB85C
#define TERM_COL_MUTED  0xFF888888
#define TERM_SOURCE_MAX 16384
#define TERM_SCX_MAX (sizeof(struct scx_header) + SOLIS_JIT_MAX_CODE + SOLIS_JIT_MAX_DATA)

#define TERM_ATTR_NORMAL 0
#define TERM_ATTR_PROMPT 1
#define TERM_ATTR_OK     2
#define TERM_ATTR_MUTED  3

static char term_buffer[TERM_BUF];
static uint8_t term_attr[TERM_BUF];
static uint8_t term_fg = TERM_ATTR_NORMAL;
static int term_row = 0;
static int term_col = 0;

static char line_buf[LINE_BUF];
static int line_pos = 0;
static char source_buffer[TERM_SOURCE_MAX + 1];
static char command_history[8][LINE_BUF];
static int command_history_count = 0;
static int command_history_head = 0;

static bool passwd_input = false;
static int passwd_stage = 0;
static int passwd_len = 0;
static char passwd_first[SETUP_PASSWORD_MAX];
static char passwd_second[SETUP_PASSWORD_MAX];

static void passwd_reset(void) {
    passwd_input = false;
    passwd_stage = 0;
    passwd_len = 0;
    passwd_first[0] = '\0';
    passwd_second[0] = '\0';
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_len(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static void term_scroll(void) {
    for (int i = 0; i < TERM_BUF - TERM_COLS; i++) {
        term_buffer[i] = term_buffer[i + TERM_COLS];
        term_attr[i] = term_attr[i + TERM_COLS];
    }
    for (int i = TERM_BUF - TERM_COLS; i < TERM_BUF; i++) {
        term_buffer[i] = ' ';
        term_attr[i] = TERM_ATTR_NORMAL;
    }
    if (term_row > 0) term_row--;
}

void cc_output_char(char c);

static void term_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
        if (term_row >= TERM_ROWS) term_scroll();
        return;
    }
    if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            term_buffer[term_row * TERM_COLS + term_col] = ' ';
            term_attr[term_row * TERM_COLS + term_col] = TERM_ATTR_NORMAL;
        }
        return;
    }
    if (c == '\t') {
        int next = (term_col + 8) & ~7;
        while (term_col < next) {
            if (term_col >= TERM_COLS) {
                term_col = 0;
                term_row++;
                if (term_row >= TERM_ROWS) term_scroll();
            }
            int tab_idx = term_row * TERM_COLS + term_col;
            if (tab_idx < TERM_BUF) {
                term_buffer[tab_idx] = ' ';
                term_attr[tab_idx] = TERM_ATTR_NORMAL;
            }
            term_col++;
        }
        return;
    }
    if (c < 32) return;
    if (term_col >= TERM_COLS) {
        term_col = 0;
        term_row++;
        if (term_row >= TERM_ROWS) term_scroll();
    }
    int idx = term_row * TERM_COLS + term_col;
    if (idx < TERM_BUF) {
        term_buffer[idx] = c;
        term_attr[idx] = term_fg;
        term_col++;
    }
}

static void term_print(const char *s) {
    while (*s) term_putchar(*s++);
}

static void term_println(const char *s) {
    term_print(s);
    term_putchar('\n');
}

static void term_clear(void) {
    for (int i = 0; i < TERM_BUF; i++) {
        term_buffer[i] = ' ';
        term_attr[i] = TERM_ATTR_NORMAL;
    }
    term_fg = TERM_ATTR_NORMAL;
    term_row = 0;
    term_col = 0;
    line_pos = 0;
}

static void int_to_str(int n, char *buf) {
    char tmp[12];
    int ti = 0;
    unsigned int value;
    if (n < 0) {
        *buf++ = '-';
        value = (unsigned int)(-(n + 1)) + 1u;
    } else {
        value = (unsigned int)n;
    }
    if (value == 0) { tmp[ti++] = '0'; }
    while (value > 0) { tmp[ti++] = '0' + (value % 10); value /= 10; }
    int bi = 0;
    while (ti > 0) buf[bi++] = tmp[--ti];
    buf[bi] = '\0';
}

static void history_add(const char *command) {
    if (!command || !command[0]) return;
    int i = 0;
    while (command[i] && i < LINE_BUF - 1) {
        command_history[command_history_head][i] = command[i];
        i++;
    }
    command_history[command_history_head][i] = '\0';
    command_history_head = (command_history_head + 1) % 8;
    if (command_history_count < 8) command_history_count++;
}

static void history_print(void) {
    if (command_history_count == 0) {
        term_println("No commands yet.");
        return;
    }
    for (int n = 0; n < command_history_count; n++) {
        int slot = (command_history_head + 8 - command_history_count + n) % 8;
        char num[8];
        int_to_str(n + 1, num);
        term_print("  ");
        term_print(num);
        term_print("  ");
        term_println(command_history[slot]);
    }
}

static void shell_prompt(void) {
    term_fg = TERM_ATTR_PROMPT;
    term_print("user@SOLIS ");
    term_print(vfs_get_cwd());
    term_print("$ ");
    term_fg = TERM_ATTR_NORMAL;
}

static int lang_has_suffix(const char *path, const char *suffix) {
    int path_len = str_len(path);
    int suffix_len = str_len(suffix);
    if (path_len < suffix_len) return 0;
    path += path_len - suffix_len;
    return str_eq(path, suffix);
}

static int read_source_file(const char *path) {
    int fd = vfs_open(path);
    if (fd < 0) {
        term_println("source: unable to open file");
        return -1;
    }

    int size = vfs_get_size(fd);
    if (size < 0 || size > TERM_SOURCE_MAX) {
        term_println("source: file is too large");
        return -1;
    }

    int bytes_read = vfs_read(fd, (uint8_t *)source_buffer, (uint32_t)size);
    if (bytes_read < 0 || bytes_read > size) {
        term_println("source: unable to read file");
        return -1;
    }
    source_buffer[bytes_read] = '\0';
    return 0;
}

void cc_output_char(char c) {
    term_putchar(c);
}

static int write_file_bytes(const char *path, const uint8_t *data, uint32_t size) {
    vfs_delete(path);
    int fd = vfs_create(path);
    if (fd < 0) return -1;
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > 2048) chunk = 2048;
        int written = vfs_write(fd, data + offset, chunk);
        if (written <= 0) return -1;
        offset += (uint32_t)written;
    }
    return 0;
}

static int read_file_bytes(const char *path, uint8_t *buffer, uint32_t capacity) {
    int fd = vfs_open(path);
    if (fd < 0) return -1;
    int size = vfs_get_size(fd);
    if (size <= 0 || (uint32_t)size > capacity) return -1;
    int read = vfs_read(fd, buffer, (uint32_t)size);
    if (read != size) return -1;
    return read;
}

static void make_scx_path(const char *source, char *output, int capacity) {
    int length = str_len(source);
    int out = 0;
    if (length > 2 && source[length - 2] == '.' && source[length - 1] == 'c') {
        while (out < length - 2 && out < capacity - 5) output[out] = source[out], out++;
    } else {
        while (out < length && out < capacity - 6) output[out] = source[out], out++;
    }
    output[out++] = '.';
    output[out++] = 's';
    output[out++] = 'c';
    output[out++] = 'x';
    output[out] = '\0';
}

static uint8_t cc_image[TERM_SCX_MAX];
static uint8_t scx_image[TERM_SCX_MAX];

static int compile_c_to_image(const char *path, uint8_t *image, uint32_t *size) {
    if (read_source_file(path) < 0) return -1;
    if (cc_compile(source_buffer, image, TERM_SCX_MAX, size) < 0) {
        term_print("C error: ");
        term_println(cc_last_error());
        return -1;
    }
    return 0;
}

static int run_scx(uint8_t *image, uint32_t size) {
    if (size < sizeof(struct scx_header)) {
        term_println("run: invalid SCX image");
        return -1;
    }
    struct scx_header *header = (struct scx_header *)image;
    if (header->magic != SOLIS_SCX_MAGIC || header->version != SOLIS_SCX_VERSION ||
        header->header_size != sizeof(struct scx_header) ||
        header->code_size == 0 || header->code_size > SOLIS_JIT_MAX_CODE ||
        header->data_size > SOLIS_JIT_MAX_DATA ||
        (uint32_t)sizeof(struct scx_header) + header->code_size + header->data_size > size) {
        term_println("run: invalid SCX image");
        return -1;
    }
    uint8_t *code = image + sizeof(struct scx_header);
    uint8_t *data = code + header->code_size;
    int status = jit_execute(code, header->code_size, data, header->data_size);
    term_print("Program exited with status ");
    char status_text[16];
    int_to_str(status, status_text);
    term_println(status_text);
    return status;
}

static void shell_compile_c(const char *path) {
    char output[96];
    uint32_t image_size = 0;
    make_scx_path(path, output, sizeof(output));
    if (compile_c_to_image(path, cc_image, &image_size) < 0) return;
    if (write_file_bytes(output, cc_image, image_size) < 0) {
        term_println("cc: could not write output image");
        return;
    }
    term_print("Compiled ");
    term_print(path);
    term_print(" -> ");
    term_print(output);
    term_print(" (");
    char size_text[16];
    int_to_str((int)image_size, size_text);
    term_print(size_text);
    term_println(" bytes)");
}

static void shell_run_source(const char *language, const char *path) {
    if (str_eq(language, "lua")) {
        if (read_source_file(path) < 0) return;
        if (lua_run(source_buffer, term_putchar) < 0) {
            term_print("Lua error: ");
            term_println(lua_last_error());
        }
        return;
    }
    if (str_eq(language, "scx")) {
        int size = read_file_bytes(path, scx_image, sizeof(scx_image));
        if (size <= 0) {
            term_println("run: cannot read SCX image");
            return;
        }
        run_scx(scx_image, (uint32_t)size);
        return;
    }
    uint32_t image_size = 0;
    if (compile_c_to_image(path, cc_image, &image_size) < 0) return;
    run_scx(cc_image, image_size);
}

static void shell_execute(const char *cmd) {
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

    if (str_eq(command, "history")) {
        history_print();
        return;
    }
    if (str_eq(command, "help")) {
        term_println("Commands:");
        term_println("  help           - show this help");
        term_println("  history        - show recent commands");
        term_println("  echo <text>    - print text");
        term_println("  clear          - clear screen");
        term_println("  ls             - list files");
        term_println("  cd [dir]       - change directory");
        term_println("  pwd            - print working directory");
        term_println("  mkdir <dir>    - create directory");
        term_println("  cat <file>     - show file contents");
        term_println("  rm <file>      - delete a file");
        term_println("  write <f> <t>  - write text to file");
        term_println("  touch <file>   - create empty file");
        term_println("  lua <file.lua> - run Lua source");
        term_println("  cc <file.c>    - compile C to native .scx");
        term_println("  run <file.c>   - compile and run C source");
        term_println("  run <file.scx> - run compiled native image");
        term_println("  cp <src> <dst> - copy a file");
        term_println("  mv <src> <dst> - rename/move a file");
        term_println("  hexdump <file> - hex view of a file");
        term_println("  uptime         - show system uptime");
        term_println("  df             - show SolFS storage usage");
        term_println("  passwd         - change your account password");
        term_println("  reboot         - restart the system");
        term_println("  calc <a> <op> <b> - calculate a+b, a-b, a*b, a/b");
        term_println("  neofetch       - Helios system overview");
        term_println("  solpkg          - launch SOLPKG package manager");
        term_println("  ping <ip>      - ICMP ping an IP address");
        term_println("  arp <ip>       - resolve MAC for an IP");
        term_println("  dmesg          - show kernel debug log");
        term_println("  sysmon         - show live system health");
        term_println("  chkhealth      - verify kernel checksum status");

    } else if (str_eq(command, "clear")) {
        term_clear();

    } else if (str_eq(command, "echo")) {
        term_println(cmd);

    } else if (str_eq(command, "pwd")) {
        term_println(vfs_get_cwd());

    } else if (str_eq(command, "df")) {
        uint64_t total = solfs_get_total_bytes();
        uint64_t free_bytes = solfs_get_free_bytes();
        if (!solfs_is_mounted() || total == 0) {
            term_println("df: SolFS is not mounted");
            return;
        }
        uint64_t used = total > free_bytes ? total - free_bytes : 0;
        char total_str[16], used_str[16], free_str[16];
        int_to_str((int)(total / 1024), total_str);
        int_to_str((int)(used / 1024), used_str);
        int_to_str((int)(free_bytes / 1024), free_str);
        term_println("Filesystem      Total    Used    Free");
        term_print("SolisFS         ");
        term_print(total_str);
        term_print(" KB  ");
        term_print(used_str);
        term_print(" KB  ");
        term_print(free_str);
        term_println(" KB");

    } else if (str_eq(command, "passwd")) {
        if (*cmd) {
            term_println("Usage: passwd");
            return;
        }
        if (!setup_is_installed()) {
            term_println("passwd: no local account is installed");
            return;
        }
        passwd_input = true;
        passwd_stage = 0;
        passwd_len = 0;
        passwd_first[0] = '\0';
        passwd_second[0] = '\0';
        term_print("New password: ");

    } else if (str_eq(command, "cd")) {
        if (vfs_cd(*cmd ? cmd : 0) != 0) {
            term_print("cd: ");
            term_print(*cmd ? cmd : "~");
            term_println(": no such directory");
        }

    } else if (str_eq(command, "mkdir")) {
        if (!*cmd) {
            term_println("Usage: mkdir <dirname>");
        } else if (vfs_mkdir(cmd) != 0) {
            term_print("mkdir: cannot create '");
            term_print(cmd);
            term_println("'");
        }

    } else if (str_eq(command, "ls")) {
        char names[SOLFS_MAX_FILES][SOLFS_MAX_NAME];
        int count = vfs_ls(names, SOLFS_MAX_FILES);
        if (count == 0) {
            term_println("(empty)");
        } else {
            for (int i = 0; i < count; i++) {
                int len = str_len(names[i]);
                if (len > 0 && names[i][len - 1] == '/') {
                    // Directory
                    term_print(names[i]);
                    term_print("  ");
                } else {
                    term_print(names[i]);
                    char fpath[64];
                    int fi = 0;
                    const char *cwd = vfs_get_cwd();
                    for (int j = 0; cwd[j]; j++) fpath[fi++] = cwd[j];
                    if (fpath[fi-1] != '/') fpath[fi++] = '/';
                    for (int j = 0; names[i][j]; j++) fpath[fi++] = names[i][j];
                    fpath[fi] = '\0';

                    int fd = vfs_open(fpath);
                    if (fd >= 0) {
                        int sz = vfs_get_size(fd);
                        term_print(" (");
                        char buf[16];
                        int_to_str(sz, buf);
                        term_print(buf);
                        term_print(" bytes)");
                    }
                    term_print("  ");
                }
            }
            term_print("\n");
        }

    } else if (str_eq(command, "cat")) {
        if (!*cmd) {
            term_println("Usage: cat <filename>");
            return;
        }
        int fd = vfs_open(cmd);
        if (fd < 0) {
            term_print("File not found: ");
            term_println(cmd);
            return;
        }
        uint8_t buf[4096];
        int n = vfs_read(fd, buf, sizeof(buf));
        if (n > 0) {
            buf[n] = '\0';
            term_print((const char *)buf);
        }
        term_print("\n");

    } else if (str_eq(command, "rm")) {
        if (!*cmd) {
            term_println("Usage: rm <filename>");
            return;
        }
        if (vfs_delete(cmd) == 0) {
            term_print("Deleted: ");
            term_println(cmd);
        } else {
            term_print("Not found: ");
            term_println(cmd);
        }

    } else if (str_eq(command, "write")) {
        char fname[32];
        int fi = 0;
        while (*cmd && *cmd != ' ' && fi < 31) {
            fname[fi++] = *cmd;
            cmd++;
        }
        fname[fi] = '\0';
        if (!fi) {
            term_println("Usage: write <filename> <content>");
            return;
        }
        while (*cmd == ' ') cmd++;
        int fd = vfs_create(fname);
        if (fd < 0) {
            fd = vfs_open(fname);
            if (fd < 0) {
                term_println("Cannot create file (storage full)");
                return;
            }
        }
        vfs_write(fd, (const uint8_t *)cmd, str_len(cmd));
        term_print("Written: ");
        term_print(fname);
        term_print("\n");

    } else if (str_eq(command, "touch")) {
        if (!*cmd) {
            term_println("Usage: touch <filename>");
            return;
        }
        if (vfs_create(cmd) < 0) {
            term_println("Cannot create file (exists or full)");
        } else {
            term_print("Created: ");
            term_println(cmd);
        }

    } else if (str_eq(command, "cp")) {
        char src[32];
        int si = 0;
        while (*cmd && *cmd != ' ' && si < 31) { src[si++] = *cmd; cmd++; }
        src[si] = '\0';
        while (*cmd == ' ') cmd++;
        if (!si || !*cmd) {
            term_println("Usage: cp <src> <dst>");
            return;
        }
        int sfd = vfs_open(src);
        if (sfd < 0) { term_println("Source not found"); return; }
        int dfd = vfs_create(cmd);
        if (dfd < 0) {
            dfd = vfs_open(cmd);
            if (dfd < 0) { term_println("Cannot create destination"); return; }
        }
        uint8_t buf[4096];
        int n = vfs_read(sfd, buf, sizeof(buf));
        vfs_write(dfd, buf, (n > 0) ? (uint32_t)n : 0);
        term_print("Copied: ");
        term_print(src);
        term_print(" -> ");
        term_println(cmd);

    } else if (str_eq(command, "mv")) {
        char src[32];
        int si = 0;
        while (*cmd && *cmd != ' ' && si < 31) { src[si++] = *cmd; cmd++; }
        src[si] = '\0';
        while (*cmd == ' ') cmd++;
        if (!si || !*cmd) {
            term_println("Usage: mv <src> <dst>");
            return;
        }
        int sfd = vfs_open(src);
        if (sfd < 0) { term_println("Source not found"); return; }
        uint8_t buf[4096];
        int n = vfs_read(sfd, buf, sizeof(buf));
        int dfd = vfs_create(cmd);
        if (dfd < 0) {
            dfd = vfs_open(cmd);
            if (dfd < 0) { term_println("Cannot create destination"); return; }
        }
        vfs_write(dfd, buf, (n > 0) ? (uint32_t)n : 0);
        vfs_delete(src);
        term_print("Moved: ");
        term_print(src);
        term_print(" -> ");
        term_println(cmd);

    } else if (str_eq(command, "hexdump")) {
        if (!*cmd) {
            term_println("Usage: hexdump <filename>");
            return;
        }
        int fd = vfs_open(cmd);
        if (fd < 0) { term_println("File not found"); return; }
        uint8_t whole[4096];
        int total = vfs_read(fd, whole, sizeof(whole));
        if (total <= 0) { term_println("(empty)"); return; }
        int offset = 0;
        while (offset < total) {
            int chunk = total - offset;
            if (chunk > 16) chunk = 16;
            uint8_t *buf = whole + offset;
            char hex[64];
            int hi = 0;
            for (int i = 0; i < chunk; i++) {
                uint8_t b = buf[i];
                hex[hi++] = "0123456789ABCDEF"[b >> 4];
                hex[hi++] = "0123456789ABCDEF"[b & 0x0F];
                hex[hi++] = ' ';
            }
            for (int i = chunk; i < 16; i++) {
                hex[hi++] = ' ';
                hex[hi++] = ' ';
                hex[hi++] = ' ';
            }
            for (int i = 0; i < chunk; i++) {
                hex[hi++] = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
            }
            hex[hi] = '\0';
            term_print(hex);
            term_print("\n");
            offset += chunk;
        }

    } else if (str_eq(command, "uptime")) {
        uint32_t ticks = timer_get_ticks();
        uint32_t secs = ticks / 100;
        uint32_t mins = secs / 60;
        uint32_t hrs = mins / 60;
        secs %= 60;
        mins %= 60;
        char buf[32];
        int bi = 0;
        if (hrs > 0) {
            char tmp[16];
            int ti = 0;
            uint32_t n = hrs;
            while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
            while (ti > 0) buf[bi++] = tmp[--ti];
            buf[bi++] = ':';
        }
        {
            char tmp[16];
            int ti = 0;
            uint32_t n = mins;
            while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
            if (ti == 0) tmp[ti++] = '0';
            while (ti > 0) buf[bi++] = tmp[--ti];
        }
        buf[bi++] = ':';
        {
            char tmp[16];
            int ti = 0;
            uint32_t n = secs;
            while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
            if (ti == 0) tmp[ti++] = '0';
            while (ti > 0) buf[bi++] = tmp[--ti];
        }
        buf[bi] = '\0';
        term_print("Uptime: ");
        term_println(buf);

    } else if (str_eq(command, "reboot")) {
        term_println("Rebooting...");
        outb(0x64, 0xFE);
        for (;;) __asm__ volatile("cli; hlt");

    } else if (str_eq(command, "calc")) {
        char a_str[16];
        int ai = 0;
        while (*cmd && *cmd != ' ' && ai < 15) { a_str[ai++] = *cmd; cmd++; }
        a_str[ai] = '\0';
        while (*cmd == ' ') cmd++;
        char op = *cmd;
        if (op) cmd++;
        while (*cmd == ' ') cmd++;
        char b_str[16];
        int bi = 0;
        while (*cmd && *cmd != ' ' && bi < 15) { b_str[bi++] = *cmd; cmd++; }
        b_str[bi] = '\0';
        if (!ai || !op || !bi) {
            term_println("Usage: calc <a> <op> <b>  (op: + - * /)");
            return;
        }
        int a = 0, b = 0;
        int sign = 1;
        const char *p = a_str;
        if (*p == '-') { sign = -1; p++; }
        while (*p) { a = a * 10 + (*p - '0'); p++; }
        a *= sign;
        sign = 1;
        p = b_str;
        if (*p == '-') { sign = -1; p++; }
        while (*p) { b = b * 10 + (*p - '0'); p++; }
        b *= sign;
        int result = 0;
        if (op == '+') result = a + b;
        else if (op == '-') result = a - b;
        else if (op == '*') result = a * b;
        else if (op == '/') {
            if (b == 0) { term_println("Division by zero"); return; }
            result = a / b;
        } else {
            term_println("Unknown operator (use + - * /)");
            return;
        }
        char res_str[16];
        int ri = 0;
        if (result < 0) { res_str[ri++] = '-'; result = -result; }
        if (result == 0) { res_str[ri++] = '0'; }
        else {
            char tmp[16];
            int ti = 0;
            int n = result;
            while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
            while (ti > 0) res_str[ri++] = tmp[--ti];
        }
        res_str[ri] = '\0';
        term_print(a_str);
        term_print(" ");
        term_print(&op);
        term_print(" ");
        term_print(b_str);
        term_print(" = ");
        term_println(res_str);

    } else if (str_eq(command, "lua")) {
        if (!*cmd) term_println("Usage: lua <file>");
        else shell_run_source("lua", cmd);

    } else if (str_eq(command, "cc")) {
        if (!*cmd) term_println("Usage: cc <file.c>");
        else shell_compile_c(cmd);

    } else if (str_eq(command, "run")) {
        if (!*cmd) {
            term_println("Usage: run <file.lua|file.c|file.scx>");
        } else if (lang_has_suffix(cmd, ".lua")) {
            shell_run_source("lua", cmd);
        } else if (lang_has_suffix(cmd, ".c")) {
            shell_run_source("c", cmd);
        } else if (lang_has_suffix(cmd, ".scx")) {
            shell_run_source("scx", cmd);
        } else {
            term_println("run: use a .lua, .c, or .scx file");
        }

    } else if (str_eq(command, "solpkg")) {
        gui_launch_app(SPX_PKG);

    } else if (str_eq(command, "ping")) {
        if (!*cmd) {
            term_println("Usage: ping <ip>");
            return;
        }
        uint8_t ip[4];
        int octet = 0, part = 0, ip_ok = 1;
        const char *s = cmd;
        for (int i = 0; i < 4; i++) {
            octet = 0; part = 0;
            while (*s >= '0' && *s <= '9') {
                octet = octet * 10 + (*s - '0');
                s++; part++;
            }
            if (part == 0 || octet > 255) { ip_ok = 0; break; }
            ip[i] = (uint8_t)octet;
            if (i < 3 && *s != '.') { ip_ok = 0; break; }
            if (i < 3) s++;
        }
        if (!ip_ok || *s) {
            term_println("Bad IP format. Use: ping a.b.c.d");
            return;
        }
        if (!net_available()) {
            term_println("Network not available.");
            return;
        }
        term_print("Pinging ");
        term_print(cmd);
        term_print("...\n");
        int result = net_ping(ip, 200);
        if (result == 0) {
            term_print("Reply received!\n");
        } else {
            term_print("No reply (timeout or no route).\n");
        }

    } else if (str_eq(command, "arp")) {
        if (!*cmd) {
            term_println("Usage: arp <ip>");
            return;
        }
        uint8_t ip[4];
        int octet = 0, part = 0, ip_ok = 1;
        const char *s = cmd;
        for (int i = 0; i < 4; i++) {
            octet = 0; part = 0;
            while (*s >= '0' && *s <= '9') {
                octet = octet * 10 + (*s - '0');
                s++; part++;
            }
            if (part == 0 || octet > 255) { ip_ok = 0; break; }
            ip[i] = (uint8_t)octet;
            if (i < 3 && *s != '.') { ip_ok = 0; break; }
            if (i < 3) s++;
        }
        if (!ip_ok || *s) {
            term_println("Bad IP format. Use: arp a.b.c.d");
            return;
        }
        uint8_t mac[6];
        int result = net_arp_resolve(ip, mac);
        if (result == 0) {
            term_print("MAC: ");
            for (int i = 0; i < 6; i++) {
                char hex[3];
                hex[0] = "0123456789ABCDEF"[mac[i] >> 4];
                hex[1] = "0123456789ABCDEF"[mac[i] & 0x0F];
                hex[2] = '\0';
                term_print(hex);
                if (i < 5) term_print(":");
            }
            term_print("\n");
        } else {
            term_println("ARP resolution failed.\n");
        }

    } else if (str_eq(command, "dmesg")) {
        char buf[512];
        int n = dbg_read(buf, 512);
        int start = 0;
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n' || i == n - 1) {
                char save = buf[i + 1];
                buf[i + 1] = '\0';
                term_println(buf + start);
                buf[i + 1] = save;
                start = i + 1;
            }
        }

    } else if (str_eq(command, "sysmon") || str_eq(command, "chkhealth")) {
        struct dbg_health health;
        dbg_get_health(&health);
        term_println("System health");
        term_print("  Pipeline: ");
        term_println(health.pipeline[0] ? health.pipeline : "idle");
        term_print("  IRQs: ");
        char tmp[32];
        int_to_str((int)health.total_irqs, tmp);
        term_print(tmp);
        term_print("  Mem: ");
        int_to_str((int)health.memory_used_kb, tmp);
        term_print(tmp);
        term_print(" KB / ");
        int_to_str((int)health.memory_total_kb, tmp);
        term_println(tmp);
        term_print("  text_crc: 0x");
        char hex[12];
        hex[0] = '0'; hex[1] = 'x';
        int hi = 2;
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nib = (health.text_crc >> i) & 0x0F;
            hex[hi++] = "0123456789ABCDEF"[nib];
        }
        hex[hi] = '\0';
        term_println(hex);
        term_print("  rodata_crc: 0x");
        hi = 2;
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nib = (health.rodata_crc >> i) & 0x0F;
            hex[hi++] = "0123456789ABCDEF"[nib];
        }
        hex[hi] = '\0';
        term_println(hex);
        term_println("  Drivers:");
        for (int i = 0; i < health.driver_count && i < DBG_MAX_DRIVERS; i++) {
            term_print("    - ");
            term_print(health.drivers[i].name);
            term_print(" : ");
            switch (health.drivers[i].state) {
                case DBG_DRIVER_ACTIVE: term_println("ACTIVE"); break;
                case DBG_DRIVER_FAILED: term_println("FAILED"); break;
                case DBG_DRIVER_STOPPED: term_println("STOPPED"); break;
                default: term_println("UNINITIALIZED"); break;
            }
        }
        if (health.driver_count == 0) {
            term_println("    - none registered");
        }
        term_println("  Status: OK");

    } else if (str_eq(command, "neofetch")) {
        term_println("  _   _   ___    ____  ");
        term_println(" | \\ | | / _ \\  / ___| ");
        term_println(" |  \\| || | | | \\___ \\ ");
        term_println(" | |\\  || |_| | ___) |");
        term_println(" |_| \\_| \\___/ |____/ ");
        term_println("                      ");
        term_println("  Solis OS 0.2          ");
        term_println("  ------------------- ");
        {
            uint32_t ticks = timer_get_ticks();
            uint32_t secs = ticks / 100;
            uint32_t mins = secs / 60;
            uint32_t hrs = mins / 60;
            secs %= 60;
            mins %= 60;
            char upbuf[32];
            int upi = 0;
            if (hrs > 0) {
                char tmp[16];
                int ti = 0;
                uint32_t n = hrs;
                while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
                while (ti > 0) upbuf[upi++] = tmp[--ti];
                upbuf[upi++] = 'h';
            }
            {
                char tmp[16];
                int ti = 0;
                uint32_t n = mins;
                while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
                if (ti == 0) tmp[ti++] = '0';
                while (ti > 0) upbuf[upi++] = tmp[--ti];
            }
            upbuf[upi++] = 'm';
            {
                char tmp[16];
                int ti = 0;
                uint32_t n = secs;
                while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
                if (ti == 0) tmp[ti++] = '0';
                while (ti > 0) upbuf[upi++] = tmp[--ti];
            }
            upbuf[upi++] = 's';
            upbuf[upi] = '\0';
            term_print("  Uptime: ");
            term_println(upbuf);
        }
        const char *cwd = vfs_get_cwd();
        term_print("  CWD:    ");
        term_println(cwd);

    } else {
        term_print("Unknown command: ");
        term_print(command);
        term_print("\n");
    }
}

void term_init(void) {
    for (int i = 0; i < TERM_BUF; i++) {
        term_buffer[i] = ' ';
        term_attr[i] = TERM_ATTR_NORMAL;
    }
    term_fg = TERM_ATTR_NORMAL;
    term_row = 0;
    term_col = 0;
    line_pos = 0;
    passwd_reset();
    term_print("Solis OS Terminal v0.2 (VFS enabled)\n");
    term_print("Type 'help' for commands.\n");
    shell_prompt();
}

void term_draw(int x, int y, int w, int h) {
    int content_x = x + 2;
    int content_y = y + 2;
    int content_w = w - 4;
    int content_h = h - 4;
    int cols = (content_w - 8) / 8;
    int rows = (content_h - 8) / 16;
    if (cols > TERM_COLS) cols = TERM_COLS;
    if (rows > TERM_ROWS) rows = TERM_ROWS;

    graphics_fill_rect(content_x, content_y, content_w, content_h, TERM_COL_BG);
    graphics_draw_rect(content_x, content_y, content_w, content_h, 0xFF172635);

    int start_row = term_row - rows + 1;
    if (start_row < 0) start_row = 0;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int bi = (start_row + r) * TERM_COLS + c;
            if (bi < TERM_BUF && term_buffer[bi]) {
                char str[2] = {term_buffer[bi], '\0'};
                uint32_t col = TERM_COL_TEXT;
                if (term_attr[bi] == TERM_ATTR_PROMPT) {
                    col = TERM_COL_ACCENT;
                } else if (term_attr[bi] == TERM_ATTR_OK) {
                    col = TERM_COL_OK;
                } else if (term_attr[bi] == TERM_ATTR_MUTED) {
                    col = TERM_COL_MUTED;
                }
                graphics_draw_string(content_x + 4 + c * 8, content_y + 4 + r * 16, str, col);
            }
        }
    }
}

void term_handle_key(char key) {
    if (passwd_input) {
        if (key == 0x1b) {
            term_putchar('\n');
            term_println("Password unchanged.");
            passwd_reset();
            shell_prompt();
            return;
        }
        if (key == '\n') {
            term_putchar('\n');
            if (passwd_len < 4) {
                term_println("Password must be at least 4 characters.");
                passwd_reset();
                shell_prompt();
                return;
            }
            if (passwd_stage == 0) {
                passwd_stage = 1;
                passwd_len = 0;
                term_print("Confirm password: ");
                return;
            }
            if (!str_eq(passwd_first, passwd_second)) {
                term_println("Passwords do not match.");
                passwd_reset();
                shell_prompt();
                return;
            }
            if (setup_change_password(passwd_first) == 0) {
                term_println("Password updated.");
            } else {
                term_println("Could not update the password.");
            }
            passwd_reset();
            shell_prompt();
            return;
        }
        if (key == '\b') {
            if (passwd_len > 0) passwd_len--;
            return;
        }
        if (key >= 32 && key < 127) {
            char *field = passwd_stage == 0 ? passwd_first : passwd_second;
            if (passwd_len < SETUP_PASSWORD_MAX - 1)
                field[passwd_len++] = key;
            field[passwd_len] = '\0';
        }
        return;
    }
    if (key == '\n') {
        line_buf[line_pos] = '\0';
        term_putchar('\n');
        history_add(line_buf);
        shell_execute(line_buf);
        line_pos = 0;
        if (!passwd_input) shell_prompt();
    } else if (key == '\b') {
        if (line_pos > 0) {
            line_pos--;
            term_putchar('\b');
        }
    } else if (key == 0x1b) {
        line_pos = 0;
    } else if (key >= 32) {
        if (line_pos < LINE_BUF - 1) {
            line_buf[line_pos++] = key;
            term_putchar(key);
        }
    }
}
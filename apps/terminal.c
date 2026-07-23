#include <nyx/apps/terminal.h>
#include <nyx/graphics.h>
#include <nyx/vfs.h>
#include <nyx/timer.h>
#include <nyx/ports.h>
#include <stdbool.h>

#define TERM_ROWS 32
#define TERM_COLS 80
#define TERM_BUF (TERM_ROWS * TERM_COLS)
#define LINE_BUF 256

static char term_buffer[TERM_BUF];
static int term_row = 0;
static int term_col = 0;

static char line_buf[LINE_BUF];
static int line_pos = 0;

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
    for (int i = 0; i < TERM_BUF - TERM_COLS; i++)
        term_buffer[i] = term_buffer[i + TERM_COLS];
    for (int i = TERM_BUF - TERM_COLS; i < TERM_BUF; i++)
        term_buffer[i] = ' ';
    if (term_row > 0) term_row--;
}

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
    for (int i = 0; i < TERM_BUF; i++)
        term_buffer[i] = ' ';
    term_row = 0;
    term_col = 0;
    line_pos = 0;
}

static void int_to_str(int n, char *buf) {
    char tmp[12];
    int ti = 0;
    if (n == 0) { tmp[ti++] = '0'; }
    while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
    int bi = 0;
    while (ti > 0) buf[bi++] = tmp[--ti];
    buf[bi] = '\0';
}

static void shell_prompt(void) {
    term_print("user@NYX ");
    term_print(vfs_get_cwd());
    term_print("$ ");
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

    if (str_eq(command, "help")) {
        term_println("Commands:");
        term_println("  help           - show this help");
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
        term_println("  cp <src> <dst> - copy a file");
        term_println("  mv <src> <dst> - rename/move a file");
        term_println("  hexdump <file> - hex view of a file");
        term_println("  uptime         - show system uptime");
        term_println("  reboot         - restart the system");
        term_println("  calc <a> <op> <b> - calculate a+b, a-b, a*b, a/b");
        term_println("  neofetch       - show system info");

    } else if (str_eq(command, "clear")) {
        term_clear();

    } else if (str_eq(command, "echo")) {
        term_println(cmd);

    } else if (str_eq(command, "pwd")) {
        term_println(vfs_get_cwd());

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
        char names[NOFS_MAX_FILES][NOFS_MAX_NAME];
        int count = vfs_ls(names, NOFS_MAX_FILES);
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

    } else if (str_eq(command, "neofetch")) {
        term_println("  _   _   ___    ____  ");
        term_println(" | \\ | | / _ \\  / ___| ");
        term_println(" |  \\| || | | | \\___ \\ ");
        term_println(" | |\\  || |_| | ___) |");
        term_println(" |_| \\_| \\___/ |____/ ");
        term_println("                      ");
        term_println("  Nyx OS 0.2          ");
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
        term_println("  Kernel: Nyx OS (i386)");
        term_println("  Shell:  myosh v2 (VFS)");
        term_println("  WM:     mywm       ");
        term_println("  Res:    1024x768   ");
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
    for (int i = 0; i < TERM_BUF; i++)
        term_buffer[i] = ' ';
    term_row = 0;
    term_col = 0;
    line_pos = 0;
    term_print("Nyx OS Terminal v0.2 (VFS enabled)\n");
    term_print("Type 'help' for commands.\n");
    shell_prompt();
}

void term_draw(int x, int y, int w, int h) {
    (void)w;
    (void)h;
    int cols = (w - 4) / 8;
    int rows = (h - 4) / 16;
    if (cols > TERM_COLS) cols = TERM_COLS;
    if (rows > TERM_ROWS) rows = TERM_ROWS;

    int start_row = term_row - rows + 1;
    if (start_row < 0) start_row = 0;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int bi = (start_row + r) * TERM_COLS + c;
            if (bi < TERM_BUF && term_buffer[bi]) {
                char str[2] = {term_buffer[bi], '\0'};
                graphics_draw_string(x + 4 + c * 8, y + 4 + r * 16, str, 0xFF00FF00);
            }
        }
    }
}

void term_handle_key(char key) {
    if (key == '\n') {
        line_buf[line_pos] = '\0';
        term_putchar('\n');
        shell_execute(line_buf);
        line_pos = 0;
        shell_prompt();
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
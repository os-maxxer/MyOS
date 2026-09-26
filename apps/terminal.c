#include <solis/apps/terminal.h>
#include <solis/graphics.h>
#include <solis/vfs.h>
#include <solis/timer.h>
#include <solis/ports.h>
#include <solis/spx.h>
#include <solis/dbg.h>
#include <solis/ata.h>
#include <solis/apps/cinterp.h>
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

#define TERM_COL_BG     0xFF11131D
#define TERM_COL_PANE   0xFF242331
#define TERM_COL_TEXT   0xFFFFF4DF
#define TERM_COL_ACCENT 0xFFFFB84D
#define TERM_COL_MUTED  0xFFAAA6B5
#define TERM_COL_OK     0xFFFFD166

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
    if (n == 0) { tmp[ti++] = '0'; }
    while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
    int bi = 0;
    while (ti > 0) buf[bi++] = tmp[--ti];
    buf[bi] = '\0';
}

static void u32_to_str(uint32_t value, char *buf) {
    char digits[12];
    int count = 0;
    if (value == 0) digits[count++] = '0';
    while (value > 0) { digits[count++] = '0' + value % 10; value /= 10; }
    int i = 0;
    while (count > 0) buf[i++] = digits[--count];
    buf[i] = '\0';
}

static void term_print_u32(uint32_t value) {
    char buf[12];
    u32_to_str(value, buf);
    term_print(buf);
}

static void term_print_hex64(uint64_t value) {
    const char *hex = "0123456789ABCDEF";
    char buf[17];
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[value & 0x0F];
        value >>= 4;
    }
    buf[16] = '\0';
    term_print(buf);
}

static void shell_prompt(void) {
    term_fg = TERM_ATTR_PROMPT;
    term_print("helios@solis ");
    term_print(vfs_get_cwd());
    term_print("$ ");
    term_fg = TERM_ATTR_NORMAL;
}

static int lang_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static int lang_parse_number(const char **cursor, int *ok) {
    int value = 0;
    int sign = 1;
    const char *p = *cursor;
    while (lang_is_space(*p)) p++;
    if (*p == '-') { sign = -1; p++; }
    if (*p < '0' || *p > '9') { *ok = 0; return 0; }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
    }
    *cursor = p;
    return value * sign;
}

static int lang_eval_expr(const char *text, int *ok) {
    const char *p = text;
    int result = lang_parse_number(&p, ok);
    if (!*ok) return 0;
    for (;;) {
        while (lang_is_space(*p)) p++;
        char op = *p;
        if (op != '+' && op != '-' && op != '*' && op != '/') break;
        p++;
        int rhs = lang_parse_number(&p, ok);
        if (!*ok) return 0;
        if (op == '+') result += rhs;
        else if (op == '-') result -= rhs;
        else if (op == '*') result *= rhs;
        else if (rhs != 0) result /= rhs;
        else { *ok = 0; return 0; }
    }
    while (lang_is_space(*p)) p++;
    if (*p != '\0' && *p != ';' && *p != ')') *ok = 0;
    return result;
}

static void lang_print_int(int value) {
    char out[16];
    int pos = 0;
    if (value < 0) { out[pos++] = '-'; value = -value; }
    if (value == 0) out[pos++] = '0';
    else {
        char digits[12];
        int count = 0;
        while (value > 0) { digits[count++] = '0' + value % 10; value /= 10; }
        while (count > 0) out[pos++] = digits[--count];
    }
    out[pos] = '\0';
    term_print(out);
}

static int lang_load(const char *path, char *source, int max) {
    int fd = vfs_open(path);
    if (fd < 0) {
        int length = str_len(path);
        int suffix = length >= 4 && path[length - 4] == '.' &&
                     path[length - 3] == 'l' && path[length - 2] == 'u' && path[length - 1] == 'a' ? 4 :
                     length >= 2 && path[length - 2] == '.' && path[length - 1] == 'c' ? 2 : 0;
        if (!suffix || length - suffix >= 64) return -1;
        char extensionless[64];
        for (int i = 0; i < length - suffix; i++) extensionless[i] = path[i];
        extensionless[length - suffix] = '\0';
        fd = vfs_open(extensionless);
    }
    if (fd < 0) return -1;
    int size = vfs_read(fd, (uint8_t *)source, (uint32_t)(max - 1));
    if (size < 0) return -1;
    source[size] = '\0';
    return size;
}

static void lang_print_segment(const char *text, int start, int end) {
    while (start < end && lang_is_space(text[start])) start++;
    while (end > start && lang_is_space(text[end - 1])) end--;

    while (end - start >= 2 && text[start] == '(' && text[end - 1] == ')') {
        int depth = 0, quoted = 0, escaped = 0, wraps = 1;
        for (int i = start; i < end; i++) {
            char c = text[i];
            if (quoted) {
                if (escaped) escaped = 0;
                else if (c == '\\') escaped = 1;
                else if (c == '"') quoted = 0;
                continue;
            }
            if (c == '"') quoted = 1;
            else if (c == '(') depth++;
            else if (c == ')' && --depth == 0 && i != end - 1) { wraps = 0; break; }
        }
        if (!wraps) break;
        start++;
        end--;
        while (start < end && lang_is_space(text[start])) start++;
        while (end > start && lang_is_space(text[end - 1])) end--;
    }

    if (end - start >= 2 && text[start] == '"' && text[end - 1] == '"') {
        for (int i = start + 1; i < end - 1; i++) {
            char c = text[i];
            if (c == '\\' && i + 1 < end - 1) {
                c = text[++i];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'r') c = '\r';
            }
            term_putchar(c);
        }
        return;
    }

    char expression[128];
    int length = end - start;
    int copy = length < (int)sizeof(expression) - 1 ? length : (int)sizeof(expression) - 1;
    for (int i = 0; i < copy; i++) expression[i] = text[start + i];
    expression[copy] = '\0';
    int ok = 1;
    int value = lang_eval_expr(expression, &ok);
    if (ok) lang_print_int(value);
    else term_print("<unsupported expression>");
}

static void lang_print_argument(const char *argument, int length) {
    while (length > 0 && lang_is_space(argument[length - 1])) length--;
    if (length > 0 && argument[length - 1] == ';') length--;
    if (length > 0 && argument[length - 1] == ')') length--;

    int start = 0, depth = 0, quoted = 0, escaped = 0;
    int concat_start = 0;
    for (int i = 0; i < length; i++) {
        char c = argument[i];
        if (quoted) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') quoted = 0;
            continue;
        }
        if (c == '"') { quoted = 1; continue; }
        if (c == '(') { depth++; continue; }
        if (c == ')') { if (depth > 0) depth--; continue; }
        if (depth != 0) continue;

        if (c == '.' && i + 1 < length && argument[i + 1] == '.') {
            lang_print_segment(argument, concat_start, i);
            concat_start = i + 2;
            i++;
        } else if (c == ',') {
            lang_print_segment(argument, concat_start, i);
            term_putchar('\t');
            start = i + 1;
            concat_start = start;
        }
    }
    lang_print_segment(argument, concat_start, length);
}

static int lang_run_lua(const char *source) {
    char names[16][24];
    char values[16][128];
    int var_count = 0;
    const char *line = source;
    while (*line) {
        const char *next = line;
        while (*next && *next != '\n') next++;

        const char *statement = line;
        while (statement < next && lang_is_space(*statement)) statement++;
        if (statement + 5 <= next && statement[0] == 'l' && statement[1] == 'o' &&
            statement[2] == 'c' && statement[3] == 'a' && statement[4] == 'l' &&
            lang_is_space(statement[5])) {
            statement += 5;
            while (statement < next && lang_is_space(*statement)) statement++;
        }

        const char *assign = statement;
        while (assign < next && ((*assign >= 'A' && *assign <= 'Z') ||
               (*assign >= 'a' && *assign <= 'z') || *assign == '_' ||
               (*assign >= '0' && *assign <= '9'))) assign++;
        const char *equals = assign;
        while (equals < next && lang_is_space(*equals)) equals++;
        if (assign > statement && equals < next && *equals == '=' &&
            (equals + 1 == next || equals[1] != '=')) {
            int name_len = (int)(assign - statement);
            int slot = -1;
            for (int v = 0; v < var_count; v++) {
                int i = 0;
                while (i < name_len && names[v][i] == statement[i]) i++;
                if (i == name_len && names[v][i] == '\0') { slot = v; break; }
            }
            if (slot < 0 && var_count < 16) slot = var_count++;
            if (slot >= 0) {
                if (name_len > 23) name_len = 23;
                for (int i = 0; i < name_len; i++) names[slot][i] = statement[i];
                names[slot][name_len] = '\0';

                const char *rhs = equals + 1;
                while (rhs < next && lang_is_space(*rhs)) rhs++;
                int out = 0, quoted = 0, escaped = 0;
                while (rhs < next && out < 126) {
                    char c = *rhs;
                    if (quoted) {
                        values[slot][out++] = c;
                        rhs++;
                        if (escaped) escaped = 0;
                        else if (c == '\\') escaped = 1;
                        else if (c == '"') quoted = 0;
                        continue;
                    }
                    if (c == '"') {
                        quoted = 1;
                        values[slot][out++] = c;
                        rhs++;
                        continue;
                    }
                    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
                        char ident[24];
                        int n = 0;
                        while (rhs < next && (((*rhs >= 'A' && *rhs <= 'Z') ||
                               (*rhs >= 'a' && *rhs <= 'z') || (*rhs >= '0' && *rhs <= '9') || *rhs == '_'))) {
                            if (n < 23) ident[n++] = *rhs;
                            rhs++;
                        }
                        ident[n] = '\0';
                        int found = -1;
                        for (int v = 0; v < var_count; v++)
                            if (str_eq(names[v], ident)) { found = v; break; }
                        if (found >= 0) {
                            for (int i = 0; values[found][i] && out < 126; i++)
                                values[slot][out++] = values[found][i];
                        } else {
                            for (int i = 0; ident[i] && out < 126; i++) values[slot][out++] = ident[i];
                        }
                        continue;
                    }
                    if (c == ';') break;
                    values[slot][out++] = c;
                    rhs++;
                }
                while (out > 0 && lang_is_space(values[slot][out - 1])) out--;
                values[slot][out] = '\0';
            }
            line = *next ? next + 1 : next;
            continue;
        }

        const char *print_call = line;
        while (print_call < next && !(print_call[0] == 'p' && print_call[1] == 'r' &&
                                      print_call[2] == 'i' && print_call[3] == 'n' &&
                                      print_call[4] == 't' && print_call[5] == '(')) print_call++;
        if (print_call < next) {
            char expanded[512];
            int out = 0, quoted = 0, escaped = 0;
            const char *arg = print_call + 6;
            while (arg < next && out < (int)sizeof(expanded) - 1) {
                char c = *arg;
                if (quoted) {
                    expanded[out++] = c;
                    arg++;
                    if (escaped) escaped = 0;
                    else if (c == '\\') escaped = 1;
                    else if (c == '"') quoted = 0;
                    continue;
                }
                if (c == '"') {
                    quoted = 1;
                    expanded[out++] = c;
                    arg++;
                    continue;
                }
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
                    char ident[24];
                    int n = 0;
                    while (arg < next && ((*arg >= 'A' && *arg <= 'Z') ||
                           (*arg >= 'a' && *arg <= 'z') || (*arg >= '0' && *arg <= '9') || *arg == '_')) {
                        if (n < 23) ident[n++] = *arg;
                        arg++;
                    }
                    ident[n] = '\0';
                    int found = -1;
                    for (int v = 0; v < var_count; v++)
                        if (str_eq(names[v], ident)) { found = v; break; }
                    if (found >= 0) {
                        for (int i = 0; values[found][i] && out < (int)sizeof(expanded) - 1; i++)
                            expanded[out++] = values[found][i];
                    } else {
                        for (int i = 0; ident[i] && out < (int)sizeof(expanded) - 1; i++)
                            expanded[out++] = ident[i];
                    }
                    continue;
                }
                expanded[out++] = c;
                arg++;
            }
            expanded[out] = '\0';
            lang_print_argument(expanded, out);
            term_putchar('\n');
        }
        line = *next ? next + 1 : next;
    }
    return 0;
}

static int lang_run_c(const char *source) {
    ci_set_putchar(term_putchar);
    int result = ci_run(source);
    const char *error = ci_last_error();
    if (error) {
        term_print("C interpreter: ");
        term_println(error);
        return -1;
    }
    if (result != 0) {
        char status[16];
        int_to_str(result, status);
        term_print("[program exited ");
        term_print(status);
        term_println("]");
    }
    return result;
}

static void shell_run_source(const char *command, const char *path) {
    char source[4097];
    if (lang_load(path, source, sizeof(source)) < 0) {
        term_print("Cannot open: ");
        term_println(path);
        return;
    }
    if (str_eq(command, "lua")) lang_run_lua(source);
    else lang_run_c(source);
}

static int lang_has_suffix(const char *path, const char *suffix) {
    int path_len = str_len(path);
    int suffix_len = str_len(suffix);
    if (path_len < suffix_len) return 0;
    return str_eq(path + path_len - suffix_len, suffix);
}

static void shell_compile_c(const char *path) {
    char source[4097];
    if (lang_load(path, source, sizeof(source)) < 0) {
        term_print("Cannot open: ");
        term_println(path);
        return;
    }
    if (ci_check(source) != 0) {
        term_print("cc: ");
        term_println(ci_last_error() ? ci_last_error() : "syntax error");
        return;
    }
    term_print("Compiled: ");
    term_println(path);
    term_println("Syntax OK. Run with: run <file.c>");
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
        term_println("  lua <file>     - run Lua print/expressions");
        term_println("  cc <file.c>    - compile-check C source");
        term_println("  run <file>     - run Lua or C source");
        term_println("  cp <src> <dst> - copy a file");
        term_println("  mv <src> <dst> - rename/move a file");
        term_println("  hexdump <file> - hex view of a file");
        term_println("  uptime         - show system uptime");
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
            term_println("Usage: run <file.lua|file.c>");
        } else if (lang_has_suffix(cmd, ".lua")) {
            shell_run_source("lua", cmd);
        } else if (lang_has_suffix(cmd, ".c")) {
            shell_run_source("c", cmd);
        } else {
            term_println("run: use a .lua or .c source file");
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
        term_println(".---..----..-.   .-..---. .----..---.");
            term_print(" \\ \\ | || || |__ | | \\ \\  | || | \\ ");
            term_putchar('\\');
            term_putchar('\n');
        term_println("`---'`----'`----'`-'`---' `----'`---'");
        term_println("");
        term_println("  Helios  |  Solis OS 1.1");
        term_println("  ----------------------");
        term_print("  Hardware ID: ");
        term_print_hex64(sys_get_machine_id());
        term_print("\n  CPU: ");
        char cpu_brand[48];
        sys_get_cpu_brand(cpu_brand, sizeof(cpu_brand));
        term_println(cpu_brand[0] ? cpu_brand : "Unknown CPU");
        term_print("  Memory: ");
        term_print_u32(sys_get_total_ram());
        term_println(" MB");
        term_print("  Display: ");
        term_print_u32(graphics_get_width());
        term_print("x");
        term_print_u32(graphics_get_height());
        term_println(" framebuffer");
        if (ata_present()) {
            const char *model = ata_get_model();
            const char *serial = ata_get_serial();
            term_print("  Disk: ");
            term_println(model && model[0] ? model : "ATA device");
            term_print("  Disk ID: ");
            term_println(serial && serial[0] ? serial : "Unavailable");
        } else {
            term_println("  Disk: No ATA device detected");
        }
        term_println("  Kernel: i386 / Multiboot2");
        term_println("  Shell: Helios");
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
    term_print("Helios shell 1.0 - Solis OS\n");
    term_print("Type 'help' to list commands.\n");
    shell_prompt();
}

void term_draw(int x, int y, int w, int h) {
    int inset = 4;
    int topbar_h = 18;
    int content_x = x + inset;
    int content_y = y + inset + topbar_h;
    int content_w = w - inset * 2;
    int content_h = h - inset * 2 - topbar_h;

    graphics_fill_rect(x, y, w, h, TERM_COL_BG);
    graphics_draw_rect(x, y, w, h, 0xFF1D2E41);
    graphics_fill_rect(x + 2, y + 2, w - 4, topbar_h, TERM_COL_PANE);
    graphics_draw_string(x + 12, y + 6, "HELIOS // SOLIS OS", TERM_COL_ACCENT);
    char resolution[24], number[12];
    u32_to_str(graphics_get_width(), number);
    int ri = 0;
    while (number[ri]) { resolution[ri] = number[ri]; ri++; }
    resolution[ri++] = 'x';
    u32_to_str(graphics_get_height(), number);
    for (int ni = 0; number[ni]; ni++) resolution[ri++] = number[ni];
    resolution[ri] = '\0';
    graphics_draw_string(x + w - 8 - ri * 8, y + 6, resolution, TERM_COL_MUTED);
    graphics_fill_rect(x + 2, y + 2 + topbar_h - 1, w - 4, 1, TERM_COL_ACCENT);

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
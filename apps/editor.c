#include <solis/apps/editor.h>
#include <solis/graphics.h>
#include <solis/gui.h>
#include <solis/vfs.h>
#include <solis/timer.h>

#define EDIT_ROWS 40
#define EDIT_COLS 100
#define EDIT_BUF (EDIT_ROWS * EDIT_COLS)
#define TOOLBAR_H 28
#define LINE_NUM_W 40
#define MAX_FILES 32
#define MAX_FNAME 24
#define TAB_W 2
#define UNDO_DEPTH 8

enum lang_mode { LANG_C, LANG_LUA, LANG_ASM, LANG_HTML, LANG_COUNT };

static char buffer[EDIT_BUF];
static int row = 0;
static int col = 0;
static int top_row = 0;
static enum lang_mode lang = LANG_C;
static char cur_file[MAX_FNAME];
static char status_msg[40];
static int status_ticks = 0;

static char undo_buffer[UNDO_DEPTH][EDIT_BUF];
static int undo_rows[UNDO_DEPTH];
static int undo_cols[UNDO_DEPTH];
static int undo_tops[UNDO_DEPTH];
static int undo_count = 0;
static int undo_head = 0;
static int goto_mode = 0;
static int goto_line = 0;

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_len(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static void int_to_str(int n, char *buf) {
    char tmp[12];
    int ti = 0;
    if (n == 0) tmp[ti++] = '0';
    while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
    int bi = 0;
    while (ti > 0) buf[bi++] = tmp[--ti];
    buf[bi] = '\0';
}

static void set_status(const char *msg) {
    int i = 0;
    while (msg[i] && i < 39) { status_msg[i] = msg[i]; i++; }
    status_msg[i] = '\0';
    status_ticks = 30;
}

static void editor_scroll_up(void) {
    for (int i = 0; i < EDIT_BUF - EDIT_COLS; i++)
        buffer[i] = buffer[i + EDIT_COLS];
    for (int i = EDIT_BUF - EDIT_COLS; i < EDIT_BUF; i++)
        buffer[i] = ' ';
    if (row > 0) row--;
}

static int count_lines_with_text(void) {
    int cnt = 0;
    for (int r = 0; r < EDIT_ROWS; r++) {
        int s = r * EDIT_COLS;
        for (int c = 0; c < EDIT_COLS; c++)
            if (buffer[s + c] != ' ') { cnt++; break; }
    }
    return cnt > 0 ? cnt : 1;
}

static int get_line_len(int r) {
    int s = r * EDIT_COLS;
    int e = s + EDIT_COLS;
    while (e > s && buffer[e-1] == ' ') e--;
    return e - s;
}

static void editor_new_line(void) {
    int src_line_start = row * EDIT_COLS;
    int indent = 0;
    while (indent < EDIT_COLS && buffer[src_line_start + indent] == ' ') indent++;

    if (row + 1 >= EDIT_ROWS) {
        editor_scroll_up();
        src_line_start = row * EDIT_COLS;
    }

    int dst_line_start = (row + 1) * EDIT_COLS;
    for (int c = 0; c < EDIT_COLS; c++)
        buffer[dst_line_start + c] = ' ';
    for (int i = 0; i < indent && i < EDIT_COLS; i++)
        buffer[dst_line_start + i] = ' ';

    int remaining = get_line_len(row) - col;
    if (remaining > 0) {
        for (int i = 0; i < remaining && indent + i < EDIT_COLS; i++)
            buffer[dst_line_start + indent + i] = buffer[src_line_start + col + i];
        for (int i = col; i < EDIT_COLS; i++)
            buffer[src_line_start + i] = ' ';
    }

    row++;
    col = indent;
}

static int is_keyword(const char *word, const char **kw_list) {
    int i = 0;
    while (kw_list[i]) {
        if (str_eq(word, kw_list[i])) return 1;
        i++;
    }
    return 0;
}

static void undo_reset(void) {
    undo_count = 0;
    undo_head = 0;
}

static void editor_push_undo(void) {
    int slot = undo_head;
    for (int i = 0; i < EDIT_BUF; i++)
        undo_buffer[slot][i] = buffer[i];
    undo_rows[slot] = row;
    undo_cols[slot] = col;
    undo_tops[slot] = top_row;
    undo_head = (undo_head + 1) % UNDO_DEPTH;
    if (undo_count < UNDO_DEPTH) undo_count++;
}

static void editor_undo(void) {
    if (undo_count == 0) {
        set_status("Nothing to undo");
        return;
    }
    int slot = (undo_head + UNDO_DEPTH - 1) % UNDO_DEPTH;
    for (int i = 0; i < EDIT_BUF; i++)
        buffer[i] = undo_buffer[slot][i];
    row = undo_rows[slot];
    col = undo_cols[slot];
    top_row = undo_tops[slot];
    undo_head = slot;
    undo_count--;
    set_status("Undo");
}

void editor_init(void) {
    for (int i = 0; i < EDIT_BUF; i++) buffer[i] = ' ';
    row = 0; col = 0; top_row = 0;
    lang = LANG_C;
    cur_file[0] = '\0';
    undo_reset();
    set_status("New file - C mode");
}

static const char *lang_name(void) {
    switch (lang) {
        case LANG_C:    return "C";
        case LANG_LUA:  return "Lua";
        case LANG_ASM:  return "ASM";
        case LANG_HTML: return "HTML";
        case LANG_COUNT: break;
    }
    return "?";
}

static enum lang_mode lang_for_path(const char *path) {
    int len = str_len(path);
    if (len >= 5 && path[len - 5] == '.' &&
        (path[len - 4] == 'h' || path[len - 4] == 'H') &&
        (path[len - 3] == 't' || path[len - 3] == 'T') &&
        (path[len - 2] == 'm' || path[len - 2] == 'M') &&
        (path[len - 1] == 'l' || path[len - 1] == 'L'))
        return LANG_HTML;
    if (len >= 4 && path[len - 4] == '.' &&
        (path[len - 3] == 'h' || path[len - 3] == 'H') &&
        (path[len - 2] == 't' || path[len - 2] == 'T') &&
        (path[len - 1] == 'm' || path[len - 1] == 'M'))
        return LANG_HTML;
    if (len >= 4 && path[len - 4] == '.' &&
        (path[len - 3] == 'l' || path[len - 3] == 'L') &&
        (path[len - 2] == 'u' || path[len - 2] == 'U') &&
        (path[len - 1] == 'a' || path[len - 1] == 'A'))
        return LANG_LUA;
    if (len >= 4 && path[len - 4] == '.' &&
        (path[len - 3] == 'a' || path[len - 3] == 'A') &&
        (path[len - 2] == 's' || path[len - 2] == 'S') &&
        (path[len - 1] == 'm' || path[len - 1] == 'M'))
        return LANG_ASM;
    if (len >= 2 && path[len - 2] == '.' &&
        (path[len - 1] == 's' || path[len - 1] == 'S'))
        return LANG_ASM;
    return LANG_C;
}

static void save_file(void) {
    char path[64];
    if (!gui_save_dialog(cur_file[0] ? cur_file : "untitled", path, 64)) {
        set_status("Save cancelled");
        return;
    }

    /* remember the basename for the file indicator */
    int pi = 0;
    while (path[pi]) pi++;
    int bi = pi;
    while (bi > 0 && path[bi - 1] != '/') bi--;
    int ci = 0;
    for (int i = bi; path[i] && ci < MAX_FNAME - 1; i++)
        cur_file[ci++] = path[i];
    cur_file[ci] = '\0';

    int fd = vfs_create(path);
    if (fd < 0) { fd = vfs_open(path); if (fd < 0) { set_status("Cannot save"); return; } }
    char out[EDIT_BUF];
    int oi = 0;
    for (int r = 0; r < EDIT_ROWS; r++) {
        int linelen = get_line_len(r);
        for (int c = 0; c < linelen && c < EDIT_COLS; c++)
            out[oi++] = buffer[r * EDIT_COLS + c];
        if (r < EDIT_ROWS - 1)
            out[oi++] = '\n';
    }
    vfs_write(fd, (const uint8_t *)out, oi);
    set_status("Saved");
}

static void editor_load_path(const char *path) {
    int fd = vfs_open(path);
    if (fd < 0) { set_status("Cannot open file"); return; }

    uint8_t contents[EDIT_BUF];
    int size = vfs_read(fd, contents, sizeof(contents));
    if (size < 0) { set_status("Cannot read file"); return; }
    for (int i = 0; i < EDIT_BUF; i++) buffer[i] = ' ';
    row = 0; col = 0; top_row = 0;
    for (int i = 0; i < size && row < EDIT_ROWS; i++) {
        char c = (char)contents[i];
        if (c == '\n') { row++; col = 0; }
        else if (c == '\r') continue;
        else if (col < EDIT_COLS) buffer[row * EDIT_COLS + col++] = c;
    }
    if (row >= EDIT_ROWS) row = EDIT_ROWS - 1;

    int len = str_len(path), start = len;
    while (start > 0 && path[start - 1] != '/') start--;
    int i = 0;
    while (path[start] && i < MAX_FNAME - 1) cur_file[i++] = path[start++];
    cur_file[i] = '\0';
    lang = lang_for_path(path);
    undo_reset();
    set_status(lang == LANG_HTML ? "Loaded HTML file" : "Loaded file");
}

void editor_open_file(const char *path) {
    if (path) editor_load_path(path);
}

static void load_file(void) {
    char path[64];
    if (!gui_open_dialog(path, sizeof(path))) {
        set_status("Load cancelled");
        return;
    }
    editor_load_path(path);
}

static void draw_text(int x, int y, const char *t, uint32_t c) {
    graphics_draw_string(x, y, t, c);
}

static uint32_t kw_color_c[]   = {0xFF0000FF, 0xFF990099}; /* keywords: blue, purple for types */
static uint32_t str_color      = 0xFFCC6600;
static uint32_t comment_color  = 0xFF008800;
static uint32_t num_color      = 0xFF009999;
static uint32_t text_color     = 0xFF222222;
static uint32_t tag_color      = 0xFF7A4BE5;
static uint32_t attr_color     = 0xFFB36A00;
static uint32_t entity_color   = 0xFF008888;

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int html_tag_known(const char *tag) {
    static const char *html_tags[] = {
        "html", "head", "body", "title", "meta", "link", "style",
        "script", "main", "header", "footer", "nav", "section",
        "article", "aside", "div", "span", "p", "a", "h1", "h2",
        "h3", "h4", "h5", "h6", "ul", "ol", "li", "table", "tr",
        "td", "th", "form", "input", "button", "label", "img", "br",
        "hr", "pre", "code", "blockquote", "strong", "em", 0
    };
    return is_keyword(tag, html_tags);
}

/* HTML gets a real tag-aware highlighter: tag names, attribute names,
 * quoted values, comments and entities each get their own color. */
static void draw_html_line(int x, int y, int line_start, int line_len,
                           int *in_comment) {
    int char_w = 8;
    int i = 0;

#define HTML_CHAR(pos, cc, col) do { \
    char _one[2] = { (cc), '\0' }; \
    graphics_draw_string(x + (pos) * char_w, y, _one, (col)); \
} while (0)

    while (i < line_len) {
        char ch = buffer[line_start + i];

        if (*in_comment) {
            HTML_CHAR(i, ch, comment_color);
            if (ch == '-' && i + 2 < line_len &&
                buffer[line_start + i + 1] == '-' &&
                buffer[line_start + i + 2] == '>') {
                HTML_CHAR(i + 1, '-', comment_color);
                HTML_CHAR(i + 2, '>', comment_color);
                i += 3;
                *in_comment = 0;
                continue;
            }
            i++;
            continue;
        }

        if (ch == '<' && i + 3 < line_len &&
            buffer[line_start + i + 1] == '!' &&
            buffer[line_start + i + 2] == '-' &&
            buffer[line_start + i + 3] == '-') {
            HTML_CHAR(i, '<', comment_color);
            HTML_CHAR(i + 1, '!', comment_color);
            HTML_CHAR(i + 2, '-', comment_color);
            HTML_CHAR(i + 3, '-', comment_color);
            i += 4;
            *in_comment = 1;
            continue;
        }

        if (ch == '<') {
            HTML_CHAR(i, '<', tag_color);
            i++;
            if (i < line_len && (buffer[line_start + i] == '/' ||
                                 buffer[line_start + i] == '!' ||
                                 buffer[line_start + i] == '?')) {
                HTML_CHAR(i, buffer[line_start + i], tag_color);
                i++;
            }

            int name_start = i;
            while (i < line_len &&
                   ((buffer[line_start + i] >= 'a' && buffer[line_start + i] <= 'z') ||
                    (buffer[line_start + i] >= 'A' && buffer[line_start + i] <= 'Z') ||
                    (buffer[line_start + i] >= '0' && buffer[line_start + i] <= '9') ||
                    buffer[line_start + i] == '-' || buffer[line_start + i] == '_' ||
                    buffer[line_start + i] == ':'))
                i++;
            int name_len = i - name_start;
            char name[32];
            int ni = 0;
            for (int k = 0; k < name_len && ni < 31; k++)
                name[ni++] = buffer[line_start + name_start + k];
            name[ni] = '\0';
            uint32_t name_color = html_tag_known(name) ? kw_color_c[0] : tag_color;
            for (int k = name_start; k < i; k++)
                HTML_CHAR(k, buffer[line_start + k], name_color);

            while (i < line_len) {
                ch = buffer[line_start + i];
                if (ch == '>') {
                    HTML_CHAR(i, ch, tag_color);
                    i++;
                    break;
                }
                if (ch == '"' || ch == '\'') {
                    char quote = ch;
                    HTML_CHAR(i, ch, str_color);
                    i++;
                    while (i < line_len) {
                        char sc = buffer[line_start + i];
                        HTML_CHAR(i, sc, str_color);
                        i++;
                        if (sc == quote) break;
                    }
                    continue;
                }
                if (ch == ' ' || ch == '\t' || ch == '/' || ch == '=') {
                    HTML_CHAR(i, ch, tag_color);
                    i++;
                    continue;
                }
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == ':') {
                    int attr_start = i;
                    while (i < line_len) {
                        char ac = buffer[line_start + i];
                        if (!((ac >= 'a' && ac <= 'z') || (ac >= 'A' && ac <= 'Z') ||
                              (ac >= '0' && ac <= '9') || ac == '-' || ac == '_' || ac == ':'))
                            break;
                        i++;
                    }
                    for (int k = attr_start; k < i; k++)
                        HTML_CHAR(k, buffer[line_start + k], attr_color);
                    continue;
                }
                HTML_CHAR(i, ch, tag_color);
                i++;
            }
            continue;
        }

        if (ch == '&') {
            int semi = -1;
            for (int k = i + 1; k < line_len && k <= i + 16; k++) {
                if (buffer[line_start + k] == ';') { semi = k; break; }
                if (!((buffer[line_start + k] >= 'a' && buffer[line_start + k] <= 'z') ||
                      (buffer[line_start + k] >= 'A' && buffer[line_start + k] <= 'Z') ||
                      (buffer[line_start + k] >= '0' && buffer[line_start + k] <= '9') ||
                      buffer[line_start + k] == '#'))
                    break;
            }
            if (semi >= 0) {
                for (int k = i; k <= semi; k++)
                    HTML_CHAR(k, buffer[line_start + k], entity_color);
                i = semi + 1;
                continue;
            }
        }

        HTML_CHAR(i, ch, text_color);
        i++;
    }
#undef HTML_CHAR
}

void editor_draw(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, TOOLBAR_H, 0xFFE8E8E8);
    graphics_draw_rect(x, y, w, TOOLBAR_H, 0xFFCCCCCC);

    int btn_y = y + 3, btn_h = 22;
    graphics_fill_rect(x + 4, btn_y, 46, btn_h, 0xFF4A90E2);
    draw_text(x + 10, btn_y + 4, "New", 0xFFFFFFFF);
    graphics_fill_rect(x + 54, btn_y, 50, btn_h, 0xFF3B8B3B);
    draw_text(x + 60, btn_y + 4, "Save", 0xFFFFFFFF);
    graphics_fill_rect(x + 108, btn_y, 50, btn_h, 0xFFD4A050);
    draw_text(x + 114, btn_y + 4, "Load", 0xFFFFFFFF);

    /* Language mode button */
    const char *lname = lang_name();
    graphics_fill_rect(x + 162, btn_y, 50, btn_h, 0xFF8B5CF6);
    draw_text(x + 168, btn_y + 4, lname, 0xFFFFFFFF);
    graphics_draw_rect(x + 162, btn_y, 50, btn_h, 0xFF7A4BE5);

    /* Language switcher arrows */
    draw_text(x + 156, btn_y + 4, "<", 0xFF444444);
    draw_text(x + 218, btn_y + 4, ">", 0xFF444444);

    if (goto_mode) {
        char prompt[32] = "Go to line: ";
        int pi = 12;
        char num[8]; int ni = 0; int n = goto_line;
        if (n == 0) num[ni++] = '0';
        while (n > 0) { num[ni++] = '0' + n % 10; n /= 10; }
        while (ni > 0) prompt[pi++] = num[--ni];
        prompt[pi] = '\0';
        draw_text(x + 230, btn_y + 4, prompt, 0xFF8B5CF6);
    } else if (status_ticks > 0 && status_msg[0]) {
        draw_text(x + 230, btn_y + 4, status_msg, 0xFF006600);
    }

    /* File indicator */
    if (cur_file[0]) {
        draw_text(x + w - 120, btn_y + 4, cur_file, 0xFF555555);
    }

    int text_x = x + 2;
    int text_top = y + TOOLBAR_H + 2;
    int text_w = w - 4;
    int text_h = h - TOOLBAR_H - 4;

    graphics_fill_rect(text_x, text_top, text_w, text_h, 0xFFFFFEF0);
    graphics_draw_rect(text_x, text_top, text_w, text_h, 0xFFCCCCCC);

    int char_w = 8;
    int char_h = 16;
    int cols = (text_w - LINE_NUM_W - 4) / char_w;
    int rows = text_h / char_h;
    if (cols > EDIT_COLS) cols = EDIT_COLS;

    /* Line numbers */
    int num_lines = count_lines_with_text();
    if (num_lines < 1) num_lines = 1;
    graphics_fill_rect(text_x, text_top, LINE_NUM_W, text_h, 0xFFE8E8E8);
    for (int r = 0; r < rows && r < num_lines; r++) {
        char ln[8];
        int_to_str(r + 1, ln);
        draw_text(text_x + LINE_NUM_W - 4 - str_len(ln) * char_w, text_top + 4 + r * char_h, ln, 0xFF888888);
    }

    /* Highlight current line */
    if (row >= top_row && row < top_row + rows) {
        int hl_y = text_top + 4 + (row - top_row) * char_h;
        graphics_fill_rect(text_x + LINE_NUM_W + 2, hl_y, cols * char_w, char_h, 0xFFFFF0D0);
    }

    /* Syntax coloring */
    const char *c_kw[] = {"int","char","void","long","short","unsigned","signed","float","double",
                          "struct","union","enum","typedef","const","static","extern","volatile",
                          "if","else","for","while","do","switch","case","break","continue",
                          "return","goto","sizeof","include","define","ifdef","endif","ifndef",
                          "#include","#define","#ifdef","#endif","#ifndef", 0};
    const char *lua_kw[] = {"and","break","do","else","elseif","end","false","for","function",
                            "if","in","local","nil","not","or","repeat","return","then",
                            "true","until","while","require","print", 0};
    const char *asm_kw[] = {"mov","add","sub","mul","div","push","pop","call","ret","jmp",
                            "je","jne","jg","jl","jge","jle","cmp","int","xor","and","or",
                            "not","shl","shr","inc","dec","nop","hlt","cli","sti",
                            "section","global","extern","align", 0};

    const char **keywords = c_kw;
    switch (lang) {
        case LANG_C:   keywords = c_kw;   break;
        case LANG_LUA: keywords = lua_kw;  break;
        case LANG_ASM: keywords = asm_kw;  break;
        case LANG_HTML: keywords = c_kw;   break;
        case LANG_COUNT: keywords = c_kw;  break;
    }

    /* Runs are drawn as contiguous same-colored strings; every character on
     * the line is emitted so delimiters, quotes and string bodies stay visible. */
#define RUN_APPEND(cc, col) do { \
    if (ri > 0 && run_color != (col)) { \
        run[ri] = '\0'; \
        draw_text(cx - ri * char_w, sy, run, run_color); \
        ri = 0; \
    } \
    run_color = (col); \
    if (ri < EDIT_COLS) run[ri++] = (cc); \
} while (0)

#define WORD_FLUSH() do { \
    if (wi > 0) { \
        word[wi] = '\0'; \
        uint32_t wc = is_keyword(word, keywords) ? kw_color_c[0] : text_color; \
        for (int _i = 0; _i < wi; _i++) RUN_APPEND(word[_i], wc); \
        wi = 0; \
    } \
} while (0)

    int html_comment = 0;
    for (int r = top_row; r < top_row + rows && r < EDIT_ROWS; r++) {
        int sy = text_top + 4 + (r - top_row) * char_h;
        int line_len = get_line_len(r);
        int cx = text_x + LINE_NUM_W + 4;

        if (lang == LANG_HTML) {
            draw_html_line(cx, sy, r * EDIT_COLS, line_len, &html_comment);
            continue;
        }

        char run[EDIT_COLS + 1];
        int ri = 0;
        uint32_t run_color = text_color;
        char word[32];
        int wi = 0;
        int in_string = 0;
        int in_comment = 0;
        int skip_next = 0;
        int pp_line = 0;

        for (int c = 0; c < line_len && c < EDIT_COLS; c++) {
            char ch = buffer[r * EDIT_COLS + c];
            uint32_t color = text_color;
            int is_ident = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';

            if (in_comment) {
                color = comment_color;
            } else if (in_string) {
                color = str_color;
                if (skip_next) {
                    skip_next = 0;
                } else if (ch == '\\') {
                    skip_next = 1;
                } else if (ch == '"') {
                    in_string = 0;
                }
            } else if (pp_line) {
                color = kw_color_c[0];
            } else if (ch == '"') {
                in_string = 1;
                color = str_color;
            } else if (ch == '/' && c + 1 < line_len && buffer[r * EDIT_COLS + c + 1] == '/') {
                in_comment = 1;
                color = comment_color;
            } else if (ch == '#') {
                pp_line = 1;
                color = kw_color_c[0];
            } else if (is_ident || (is_digit(ch) && wi > 0)) {
                if (wi < 31) word[wi++] = ch;
                cx += char_w;
                continue;
            } else if (is_digit(ch)) {
                color = num_color;
            }

            WORD_FLUSH();
            RUN_APPEND(ch, color);
            cx += char_w;
        }
        WORD_FLUSH();
        if (ri > 0) {
            run[ri] = '\0';
            draw_text(cx - ri * char_w, sy, run, run_color);
        }
    }

#undef RUN_APPEND
#undef WORD_FLUSH

    /* Cursor */
    if (row >= top_row && row < top_row + rows) {
        int csr_x = text_x + LINE_NUM_W + 4 + col * char_w;
        int csr_y = text_top + 4 + (row - top_row) * char_h;
        char csr_str[2] = "|";
        draw_text(csr_x, csr_y, csr_str, 0xFF000000);
    }

    if (status_ticks > 0) status_ticks--;
}

void editor_handle_key(char key) {
    if (goto_mode) {
        if (key >= '0' && key <= '9') {
            if (goto_line < 100000) goto_line = goto_line * 10 + (key - '0');
        } else if (key == '\n' || key == '\r') {
            int target = goto_line > 0 ? goto_line - 1 : 0;
            if (target >= EDIT_ROWS) target = EDIT_ROWS - 1;
            row = target;
            col = 0;
            top_row = row > 10 ? row - 10 : 0;
            goto_mode = 0;
            set_status("Line selected");
        } else if (key == 0x1b) {
            goto_mode = 0;
            set_status("Go to cancelled");
        }
        return;
    }
    /* Ctrl+G = go to line */
    if (key == 0x07) { goto_mode = 1; goto_line = 0; return; }
    /* Ctrl+Z = undo */
    if (key == 0x1A) { editor_undo(); return; }
    /* Ctrl+S = save */
    if (key == 0x13) { save_file(); return; }
    /* Ctrl+O = open file dialog (simplified) */
    if (key == 0x0F) { set_status("Open: use Load button"); return; }
    /* Ctrl+N = new */
    if (key == 0x0E) {
        editor_push_undo();
        for (int i = 0; i < EDIT_BUF; i++) buffer[i] = ' ';
        row = 0; col = 0; top_row = 0; cur_file[0] = '\0';
        set_status("New file");
        return;
    }

    if (key == '\n') {
        editor_push_undo();
        editor_new_line();
    } else if (key == '\b') {
        editor_push_undo();
        if (col > 0) {
            col--;
            buffer[row * EDIT_COLS + col] = ' ';
        } else if (row > 0) {
            int prev_len = get_line_len(row - 1);
            int cur_len = get_line_len(row);
            for (int i = 0; i < cur_len && prev_len + i < EDIT_COLS; i++)
                buffer[(row - 1) * EDIT_COLS + prev_len + i] = buffer[row * EDIT_COLS + i];
            for (int i = 0; i < EDIT_COLS; i++)
                buffer[row * EDIT_COLS + i] = ' ';
            row--;
            col = prev_len;
        }
    } else if (key == '\t') {
        editor_push_undo();
        for (int i = 0; i < TAB_W && col < EDIT_COLS; i++) {
            buffer[row * EDIT_COLS + col] = ' ';
            col++;
        }
    } else if (key >= 32) {
        editor_push_undo();
        if (col >= EDIT_COLS) { col = 0; if (row + 1 < EDIT_ROWS) row++; }
        buffer[row * EDIT_COLS + col] = key;
        col++;
    }
}

void editor_handle_mouse(int win_x, int win_y, int win_w, int win_h, int mx, int my) {
    (void)win_w; (void)win_h;
    int rel_x = mx - win_x;
    int rel_y = my - win_y;

    if (rel_y >= 3 && rel_y < 25 && rel_x >= 0 && rel_x < win_w) {
        if (rel_x >= 4 && rel_x < 50) {
            editor_init();
            return;
        }
        if (rel_x >= 54 && rel_x < 104) {
            save_file();
            return;
        }
        if (rel_x >= 108 && rel_x < 158) {
            load_file();
            return;
        }
        if (rel_x >= 156 && rel_x < 162) {
            /* Prev language */
            lang = (enum lang_mode)((lang + LANG_COUNT - 1) % LANG_COUNT);
            set_status(lang_name());
            return;
        }
        if (rel_x >= 218 && rel_x < 224) {
            /* Next language */
            lang = (enum lang_mode)((lang + 1) % LANG_COUNT);
            set_status(lang_name());
            return;
        }
    }

    /* Click in text area to position cursor */
    int text_x = win_x + 2;
    int text_top = win_y + TOOLBAR_H + 2;
    if (rel_x >= text_x && rel_y >= text_top) {
        int char_w = 8;
        int char_h = 16;
        int clk_row = (rel_y - text_top - 4) / char_h;
        int clk_col = (rel_x - text_x - LINE_NUM_W - 4) / char_w;
        if (clk_row >= 0 && clk_row < EDIT_ROWS && clk_col >= 0 && clk_col < EDIT_COLS) {
            row = clk_row;
            col = clk_col;
        }
    }
}

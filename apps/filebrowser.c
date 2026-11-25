/* Files - the Solis file manager.
 *
 * Dark two-pane browser: places sidebar, toolbar with navigation and a
 * search field, sortable Name/Size/Type columns, a details inspector for
 * text files, folder creation, a delete confirmation and a right-click
 * menu. Hit testing and painting share the same layout helpers, so click
 * targets cannot drift away from what is on screen.
 */

#include <solis/apps/filebrowser.h>
#include <solis/graphics.h>
#include <solis/vfs.h>
#include <solis/solfs.h>
#include <solis/gui.h>
#include <solis/spx.h>
#include <solis/timer.h>
#include <stdbool.h>

#define C_BG          0xFF101827
#define C_PANEL       0xFF141E2E
#define C_TOOLBAR     0xFF182335
#define C_SURFACE     0xFF162033
#define C_SURFACE_HI  0xFF1E2A40
#define C_ROW_ALT     0xFF131C2B
#define C_STROKE      0xFF2C3A52
#define C_TEXT        0xFFF3F7FF
#define C_DIM         0xFF9AA8BD
#define C_MUTED       0xFF6C7C93
#define C_ACCENT      0xFF5B8DEF
#define C_ACCENT_SOFT 0xFF31506F
#define C_AMBER       0xFFE4B363
#define C_DANGER      0xFFE05B5B

#define SIDEBAR_W  152
#define TOOLBAR_H  38
#define HEADER_H   26
#define ROW_H      24
#define STATUS_H   24
#define INSPECT_W  190
#define PATH_MAX_S 80
#define QUERY_MAX  24
#define PREVIEW_MAX 768

static int ox, oy;

static char current_path[PATH_MAX_S];
static char entries[SOLFS_MAX_FILES][SOLFS_MAX_NAME];
static int  sizes[SOLFS_MAX_FILES];
static int  entry_count = 0;
static int  view[SOLFS_MAX_FILES];
static int  view_count = 0;
static int  selected = -1;
static int  scroll = 0;
static int  sort_mode = 0;
static bool sort_desc = false;
static char query[QUERY_MAX];
static int  query_len = 0;
static bool search_active = false;
static bool new_open = false;
static char new_name[SOLFS_MAX_NAME];
static int  new_len = 0;
static bool delete_open = false;
static bool props_open = false;
static bool ctx_open = false;
static bool ctx_sub = false;
static int  ctx_x = 0, ctx_y = 0, ctx_entry = -1;
static int  last_click = -1;
static uint32_t last_click_tick = 0;
static int  last_rows = 12;
static char preview[PREVIEW_MAX];
static int  preview_len = 0;

static int str_len(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static bool str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void str_cpy(char *dst, const char *src, int max) {
    int i = 0;
    if (max <= 0) return;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void str_cat(char *dst, const char *src, int max) {
    int n = str_len(dst);
    if (n >= max - 1) return;
    str_cpy(dst + n, src, max - n);
}

static char lowerc(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static void append_str(char *buf, int *pos, const char *s, int max) {
    while (*s && *pos < max - 1) buf[(*pos)++] = *s++;
    buf[*pos] = '\0';
}

static bool contains_ci(const char *hay, const char *needle) {
    int n = str_len(needle);
    if (n == 0) return true;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (j < n && hay[i + j] && lowerc(hay[i + j]) == lowerc(needle[j])) j++;
        if (j == n) return true;
    }
    return false;
}

static int name_cmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = lowerc(*a), cb = lowerc(*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static bool is_dir(const char *name) {
    int n = str_len(name);
    return n > 0 && name[n - 1] == '/';
}

static void entry_label(int idx, char *out, int max) {
    str_cpy(out, entries[idx], max);
    int n = str_len(out);
    if (n > 0 && out[n - 1] == '/') out[n - 1] = '\0';
}

static void path_join(char *out, int max, const char *dir, const char *name) {
    str_cpy(out, dir, max);
    int n = str_len(out);
    if (n > 0 && out[n - 1] != '/' && n < max - 1) {
        out[n++] = '/';
        out[n] = '\0';
    }
    str_cat(out, name, max);
}

static void entry_path(int idx, char *out, int max) {
    if (idx < 0 || idx >= entry_count) { out[0] = '\0'; return; }
    path_join(out, max, current_path, entries[idx]);
    int n = str_len(out);
    if (n > 0 && out[n - 1] == '/') out[n - 1] = '\0';
}

static void int_to_str(int n, char *buf) {
    char tmp[12];
    int ti = 0;
    if (n == 0) tmp[ti++] = '0';
    while (n > 0) { tmp[ti++] = (char)('0' + (n % 10)); n /= 10; }
    int bi = 0;
    while (ti > 0) buf[bi++] = tmp[--ti];
    buf[bi] = '\0';
}

static void format_size(int bytes, char *out, int max) {
    if (bytes < 0) bytes = 0;
    if (bytes < 1024) {
        int_to_str(bytes, out);
        int pos = str_len(out);
        append_str(out, &pos, " B", max);
        return;
    }
    int kb10 = (bytes * 10 + 512) / 1024;
    int_to_str(kb10 / 10, out);
    int pos = str_len(out);
    char frac[2];
    frac[0] = (char)('0' + kb10 % 10);
    frac[1] = '\0';
    append_str(out, &pos, ".", max);
    append_str(out, &pos, frac, max);
    append_str(out, &pos, " KB", max);
}

static bool has_ext(const char *name, int len, const char *ext) {
    int e = str_len(ext);
    if (len < e + 2 || name[len - e - 1] != '.') return false;
    for (int i = 0; i < e; i++)
        if (lowerc(name[len - e + i]) != ext[i]) return false;
    return true;
}

static const char *type_for(const char *name) {
    int n = str_len(name);
    if (is_dir(name)) return "Folder";
    if (has_ext(name, n, "c") || has_ext(name, n, "h")) return "C source";
    if (has_ext(name, n, "lua")) return "Lua script";
    if (has_ext(name, n, "html") || has_ext(name, n, "htm")) return "Web page";
    if (has_ext(name, n, "md")) return "Markdown";
    if (has_ext(name, n, "txt")) return "Text";
    if (has_ext(name, n, "spx")) return "App package";
    return "File";
}

static int entry_size(int idx) {
    if (idx < 0 || idx >= entry_count || is_dir(entries[idx])) return 0;
    char path[PATH_MAX_S];
    entry_path(idx, path, PATH_MAX_S);
    int fd = vfs_open(path);
    if (fd < 0) return 0;
    int sz = vfs_get_size(fd);
    return sz < 0 ? 0 : sz;
}

static int default_app_for_file(const char *name) {
    int n = str_len(name);
    if (has_ext(name, n, "c") || has_ext(name, n, "h") ||
        has_ext(name, n, "lua")) return SPX_EDITOR;
    if (has_ext(name, n, "html") || has_ext(name, n, "htm")) return SPX_BROWSER;
    return SPX_NOTEPAD;
}

static int cmp_entries(int a, int b) {
    bool da = is_dir(entries[a]), db = is_dir(entries[b]);
    if (da != db) return da ? -1 : 1;
    int c = 0;
    if (sort_mode == 1) {
        if (sizes[a] != sizes[b]) c = sizes[a] < sizes[b] ? -1 : 1;
    } else if (sort_mode == 2) {
        c = name_cmp(type_for(entries[a]), type_for(entries[b]));
    }
    if (c == 0) c = name_cmp(entries[a], entries[b]);
    return sort_desc ? -c : c;
}

static void clamp_scroll(int rows) {
    if (rows < 1) rows = 1;
    int maxs = view_count - rows;
    if (maxs < 0) maxs = 0;
    if (scroll > maxs) scroll = maxs;
    if (scroll < 0) scroll = 0;
}

static void refresh_list(void) {
    entry_count = vfs_ls_at(current_path, entries, SOLFS_MAX_FILES);
    if (entry_count < 0) entry_count = 0;
    if (entry_count > SOLFS_MAX_FILES) entry_count = SOLFS_MAX_FILES;
    for (int i = 0; i < entry_count; i++)
        sizes[i] = is_dir(entries[i]) ? 0 : entry_size(i);

    view_count = 0;
    for (int i = 0; i < entry_count; i++) {
        if (query_len == 0 || contains_ci(entries[i], query))
            view[view_count++] = i;
    }
    for (int i = 1; i < view_count; i++) {
        int key = view[i];
        int j = i - 1;
        while (j >= 0 && cmp_entries(key, view[j]) < 0) {
            view[j + 1] = view[j];
            j--;
        }
        view[j + 1] = key;
    }

    if (selected >= 0) {
        bool found = false;
        for (int i = 0; i < view_count; i++)
            if (view[i] == selected) found = true;
        if (!found) selected = -1;
    }
    clamp_scroll(last_rows);
}

static void load_preview(int idx) {
    preview_len = 0;
    if (idx < 0 || idx >= entry_count || is_dir(entries[idx])) return;
    char path[PATH_MAX_S];
    entry_path(idx, path, PATH_MAX_S);
    int fd = vfs_open(path);
    if (fd < 0) return;
    int sz = vfs_get_size(fd);
    if (sz > PREVIEW_MAX) sz = PREVIEW_MAX;
    if (sz > 0) preview_len = vfs_read(fd, (uint8_t *)preview, (uint32_t)sz);
    if (preview_len < 0 || preview_len > PREVIEW_MAX) preview_len = 0;
}

static void select_entry(int idx) {
    if (idx < 0 || idx >= entry_count) return;
    selected = idx;
    load_preview(idx);
}

static void navigate_to(const char *path) {
    str_cpy(current_path, (path && path[0]) ? path : "/", PATH_MAX_S);
    selected = -1;
    scroll = 0;
    preview_len = 0;
    search_active = false;
    query_len = 0;
    query[0] = '\0';
    refresh_list();
}

static void go_up(void) {
    int len = str_len(current_path);
    if (len <= 1) return;
    int i = len - 1;
    while (i > 0 && current_path[i] != '/') i--;
    if (i == 0) navigate_to("/");
    else {
        current_path[i] = '\0';
        navigate_to(current_path);
    }
}

static void open_entry(int idx) {
    if (idx < 0 || idx >= entry_count) return;
    char path[PATH_MAX_S];
    entry_path(idx, path, PATH_MAX_S);
    if (is_dir(entries[idx])) {
        navigate_to(path);
        return;
    }
    gui_open_with(default_app_for_file(entries[idx]), path);
}

static void set_sort(int mode) {
    if (mode == sort_mode) sort_desc = !sort_desc;
    else { sort_mode = mode; sort_desc = false; }
    scroll = 0;
    refresh_list();
}

static void create_folder(void) {
    if (new_len <= 0) { new_open = false; return; }
    new_name[new_len] = '\0';
    char path[PATH_MAX_S];
    path_join(path, PATH_MAX_S, current_path, new_name);
    if (vfs_mkdir(path) == 0) {
        gui_notify("Files", "Folder created");
        new_open = false;
        refresh_list();
        char label[SOLFS_MAX_NAME];
        for (int i = 0; i < entry_count; i++) {
            entry_label(i, label, SOLFS_MAX_NAME);
            if (name_cmp(label, new_name) == 0) {
                selected = i;
                load_preview(i);
                break;
            }
        }
    } else {
        gui_notify("Files", "Could not create folder");
    }
}

static void delete_selected(void) {
    if (selected < 0 || selected >= entry_count || is_dir(entries[selected])) {
        delete_open = false;
        return;
    }
    char path[PATH_MAX_S];
    entry_path(selected, path, PATH_MAX_S);
    if (vfs_delete(path) == 0) gui_notify("Files", "Item deleted");
    else gui_notify("Files", "Could not delete item");
    delete_open = false;
    selected = -1;
    preview_len = 0;
    refresh_list();
}

void filebrowser_init(void) {
    str_cpy(current_path, "/home", PATH_MAX_S);
    selected = -1;
    scroll = 0;
    sort_mode = 0;
    sort_desc = false;
    query_len = 0;
    query[0] = '\0';
    search_active = false;
    new_open = false;
    delete_open = false;
    props_open = false;
    ctx_open = false;
    ctx_sub = false;
    preview_len = 0;
    last_click = -1;
    refresh_list();
}

void filebrowser_get_size(int *w, int *h) {
    if (w) *w = 800;
    if (h) *h = 480;
}

/* ------------------------------------------------------------------ */
/* Client-space drawing helpers                                        */
/* ------------------------------------------------------------------ */
static void fr(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    graphics_fill_rect((uint32_t)(ox + x), (uint32_t)(oy + y), (uint32_t)w, (uint32_t)h, c);
}

/* The SPX runtime exposes the flat primitives only, so the rounded helpers
 * below paint square corners. Keeping the call sites unchanged means the
 * layout code never has to care which primitives the runtime exports. */
static void frr(int x, int y, int w, int h, int r, uint32_t c) {
    (void)r;
    fr(x, y, w, h, c);
}

static void drr(int x, int y, int w, int h, int r, uint32_t c) {
    (void)r;
    if (w <= 0 || h <= 0) return;
    graphics_draw_rect((uint32_t)(ox + x), (uint32_t)(oy + y), (uint32_t)w, (uint32_t)h, c);
}

static void tx(int x, int y, const char *s, uint32_t c) {
    if (!s || !s[0]) return;
    graphics_draw_string((uint32_t)(ox + x), (uint32_t)(oy + y), s, c);
}

static void txc(int x, int y, const char *s, int max_w, uint32_t c) {
    int maxc = max_w / 8;
    int n = str_len(s);
    if (maxc < 3) return;
    if (n <= maxc) { tx(x, y, s, c); return; }
    char buf[SOLFS_MAX_NAME + 8];
    int lim = maxc - 2;
    if (lim > SOLFS_MAX_NAME + 3) lim = SOLFS_MAX_NAME + 3;
    for (int i = 0; i < lim; i++) buf[i] = s[i];
    buf[lim] = '.';
    buf[lim + 1] = '.';
    buf[lim + 2] = '\0';
    tx(x, y, buf, c);
}

static void trx(int right, int y, const char *s, uint32_t c) {
    tx(right - str_len(s) * 8, y, s, c);
}

static void hline(int x, int y, int w, uint32_t c) {
    fr(x, y, w, 1, c);
}

/* ------------------------------------------------------------------ */
/* Layout (shared by painting and hit testing)                         */
/* ------------------------------------------------------------------ */
static bool has_inspector(int w) { return w - SIDEBAR_W >= 560; }
static int inspector_w(int w) { return has_inspector(w) ? INSPECT_W : 0; }
static int content_w(int w) { return w - SIDEBAR_W - inspector_w(w); }
static int name_x(void) { return SIDEBAR_W + 30; }
static int size_right(int w) { return SIDEBAR_W + content_w(w) - 64; }
static int type_right(int w) { return SIDEBAR_W + content_w(w) - 10; }

static int list_rows(int h) {
    int lh = h - TOOLBAR_H - HEADER_H - STATUS_H;
    int r = lh / ROW_H;
    return r < 1 ? 1 : r;
}

static void draw_button(int x, int y, int w, int h, const char *label, bool primary) {
    frr(x, y, w, h, 4, primary ? C_ACCENT : C_SURFACE_HI);
    drr(x, y, w, h, 4, primary ? C_ACCENT : C_STROKE);
    tx(x + (w - str_len(label) * 8) / 2, y + (h - 16) / 2, label,
       primary ? C_TEXT : C_DIM);
}

static void draw_toolbar(int w) {
    fr(0, 0, w, TOOLBAR_H, C_TOOLBAR);
    hline(0, TOOLBAR_H - 1, w, C_STROKE);
    draw_button(8, 7, 42, 24, "Up", false);
    draw_button(54, 7, 72, 24, "Refresh", false);
    draw_button(130, 7, 64, 24, "+ New", false);

    int bx = 202;
    int bw = w - bx - 8;
    if (bw < 60) return;
    if (search_active) {
        frr(bx, 7, bw, 24, 4, C_BG);
        drr(bx, 7, bw, 24, 4, C_ACCENT);
        graphics_fill_circle((uint32_t)(ox + bx + 13), (uint32_t)(oy + 19), 4, C_MUTED);
        fr(bx + 16, 22, 5, 1, C_MUTED);
        const char *shown = query_len ? query : "Search this folder";
        tx(bx + 26, 11, shown, query_len ? C_TEXT : C_MUTED);
        if (((timer_get_ticks() / 20) & 1) == 0) {
            int caret = bx + 26 + query_len * 8;
            fr(caret, 10, 1, 16, C_ACCENT);
        }
    } else {
        frr(bx, 7, bw, 24, 4, C_SURFACE);
        drr(bx, 7, bw, 24, 4, C_STROKE);
        tx(bx + 10, 11, "/", C_MUTED);
        txc(bx + 22, 11, current_path, bw - 30, C_DIM);
    }
}

static const struct place {
    const char *label;
    const char *path;
} places[] = {
    {"Home", "/home"},
    {"Documents", "/docs"},
    {"Downloads", "/downloads"},
    {"Desktop", "/desktop"},
    {"Computer", "/"},
};
#define PLACE_COUNT ((int)(sizeof(places) / sizeof(places[0])))

static void draw_sidebar(int h) {
    fr(0, TOOLBAR_H, SIDEBAR_W, h - TOOLBAR_H, C_PANEL);
    fr(SIDEBAR_W - 1, TOOLBAR_H, 1, h - TOOLBAR_H, C_STROKE);
    tx(14, TOOLBAR_H + 12, "PLACES", C_MUTED);

    for (int i = 0; i < PLACE_COUNT; i++) {
        int ry = TOOLBAR_H + 34 + i * 30;
        bool active = str_eq(current_path, places[i].path);
        if (active) {
            frr(8, ry, SIDEBAR_W - 16, 26, 4, C_ACCENT_SOFT);
            fr(8, ry + 4, 3, 18, C_ACCENT);
        }
        fr(18, ry + 11, 12, 9, C_AMBER);
        fr(20, ry + 8, 6, 3, C_AMBER);
        tx(36, ry + 5, places[i].label, active ? C_TEXT : C_DIM);
    }

    int by = h - STATUS_H - 42;
    if (by > TOOLBAR_H + 190) {
        hline(12, by - 10, SIDEBAR_W - 24, C_STROKE);
        draw_button(10, by, SIDEBAR_W - 20, 28, "+ New folder", false);
    }
}

static void draw_list_header(int w) {
    int rx = SIDEBAR_W;
    int rw = content_w(w);
    fr(rx, TOOLBAR_H, rw, HEADER_H, C_PANEL);
    hline(rx, TOOLBAR_H + HEADER_H - 1, rw, C_STROKE);

    char hdr[16];
    int pos = 0;
    const char *label = sort_mode == 0 ? "Name" : (sort_mode == 1 ? "Size" : "Type");
    append_str(hdr, &pos, label, 16);
    if (sort_mode == 0) append_str(hdr, &pos, sort_desc ? " v" : " ^", 16);
    tx(name_x(), TOOLBAR_H + 5, hdr, C_TEXT);

    pos = 0;
    append_str(hdr, &pos, "Size", 16);
    if (sort_mode == 1) append_str(hdr, &pos, sort_desc ? " v" : " ^", 16);
    trx(size_right(w), TOOLBAR_H + 5, hdr, sort_mode == 1 ? C_TEXT : C_MUTED);

    pos = 0;
    append_str(hdr, &pos, "Type", 16);
    if (sort_mode == 2) append_str(hdr, &pos, sort_desc ? " v" : " ^", 16);
    trx(type_right(w), TOOLBAR_H + 5, hdr, sort_mode == 2 ? C_TEXT : C_MUTED);
}

static void draw_rows(int w, int h) {
    int rx = SIDEBAR_W;
    int rw = content_w(w);
    int rows = list_rows(h);
    int ly = TOOLBAR_H + HEADER_H;
    int name_w = size_right(w) - 10 - name_x();

    for (int i = 0; i < rows; i++) {
        int pos = scroll + i;
        int ry = ly + i * ROW_H;
        if (pos >= view_count) {
            fr(rx, ry, rw, ROW_H, C_BG);
            continue;
        }
        int idx = view[pos];
        bool sel = idx == selected;
        uint32_t bg = sel ? C_ACCENT_SOFT : ((i & 1) ? C_ROW_ALT : C_BG);
        fr(rx, ry, rw, ROW_H, bg);
        if (sel) fr(rx, ry, 3, ROW_H, C_ACCENT);

        char label[SOLFS_MAX_NAME];
        entry_label(idx, label, SOLFS_MAX_NAME);
        if (is_dir(entries[idx])) {
            fr(rx + 10, ry + 10, 14, 10, C_AMBER);
            fr(rx + 12, ry + 7, 6, 3, C_AMBER);
        } else {
            fr(rx + 11, ry + 5, 11, 15, C_MUTED);
            fr(rx + 13, ry + 9, 7, 1, C_BG);
            fr(rx + 13, ry + 12, 7, 1, C_BG);
            fr(rx + 13, ry + 15, 4, 1, C_BG);
        }
        txc(name_x(), ry + 4, label, name_w, sel ? C_TEXT : C_DIM);

        char sz[16];
        if (is_dir(entries[idx])) str_cpy(sz, "-", sizeof(sz));
        else format_size(sizes[idx], sz, sizeof(sz));
        trx(size_right(w), ry + 4, sz, sel ? C_TEXT : C_MUTED);
        trx(type_right(w), ry + 4, type_for(entries[idx]), sel ? C_TEXT : C_MUTED);
    }

    if (view_count > rows) {
        int track_x = rx + rw - 6;
        fr(track_x, ly, 6, rows * ROW_H, C_SURFACE);
        int thumb_h = rows * rows * ROW_H / view_count;
        if (thumb_h < 20) thumb_h = 20;
        int span = rows * ROW_H - thumb_h;
        int maxs = view_count - rows;
        int thumb_y = ly + (maxs ? scroll * span / maxs : 0);
        fr(track_x, thumb_y, 6, thumb_h, C_ACCENT_SOFT);
    }
}

static void draw_inspector(int w, int h) {
    int x = SIDEBAR_W + content_w(w);
    int y = TOOLBAR_H;
    int ih = h - TOOLBAR_H - STATUS_H;
    fr(x, y, inspector_w(w), ih, C_PANEL);
    fr(x, y, 1, ih, C_STROKE);
    tx(x + 14, y + 12, "DETAILS", C_MUTED);

    if (selected < 0) {
        tx(x + 14, y + 40, "No selection", C_DIM);
        tx(x + 14, y + 60, "Select a file", C_MUTED);
        return;
    }

    char label[SOLFS_MAX_NAME];
    entry_label(selected, label, SOLFS_MAX_NAME);
    txc(x + 14, y + 36, label, INSPECT_W - 28, C_TEXT);
    tx(x + 14, y + 58, type_for(entries[selected]), C_DIM);

    char sz[16];
    if (!is_dir(entries[selected])) {
        format_size(sizes[selected], sz, sizeof(sz));
        tx(x + 14, y + 76, sz, C_DIM);
    }
    hline(x + 14, y + 98, INSPECT_W - 28, C_STROKE);

    if (is_dir(entries[selected])) {
        tx(x + 14, y + 110, "Folder", C_MUTED);
        return;
    }
    tx(x + 14, y + 110, "PREVIEW", C_MUTED);

    int cols = (INSPECT_W - 28) / 8;
    if (cols > 20) cols = 20;
    int maxrows = (ih - 142) / 16;
    if (maxrows < 1) return;
    int pos = 0, row = 0;
    while (pos < preview_len && row < maxrows) {
        char line[24];
        int n = 0;
        while (pos < preview_len && preview[pos] != '\n' && n < cols) {
            char ch = preview[pos++];
            if (ch == '\r') continue;
            if (ch == '\t') {
                for (int k = 0; k < 4 && n < cols; k++) line[n++] = ' ';
            } else if (ch >= 32 && ch < 127) {
                line[n++] = ch;
            } else {
                line[n++] = ' ';
            }
        }
        if (pos < preview_len && preview[pos] == '\n') {
            pos++;
        } else if (n == cols) {
            while (pos < preview_len && preview[pos] != '\n') pos++;
            if (pos < preview_len) pos++;
        }
        line[n] = '\0';
        tx(x + 14, y + 132 + row * 16, line, C_DIM);
        row++;
    }
    if (preview_len == 0) tx(x + 14, y + 132, "Empty file", C_MUTED);
}

static void draw_status(int w, int h) {
    int sy = h - STATUS_H;
    fr(0, sy, w, STATUS_H, C_TOOLBAR);
    hline(0, sy, w, C_STROKE);

    char buf[80];
    int pos = 0;
    char num[12];
    int_to_str(view_count, num);
    append_str(buf, &pos, num, sizeof(buf));
    append_str(buf, &pos, view_count == 1 ? " item" : " items", sizeof(buf));
    if (selected >= 0) {
        char label[SOLFS_MAX_NAME];
        entry_label(selected, label, SOLFS_MAX_NAME);
        append_str(buf, &pos, "  -  ", sizeof(buf));
        append_str(buf, &pos, label, sizeof(buf));
        if (!is_dir(entries[selected])) {
            char sz[16];
            format_size(sizes[selected], sz, sizeof(sz));
            append_str(buf, &pos, "  -  ", sizeof(buf));
            append_str(buf, &pos, sz, sizeof(buf));
        }
    }
    txc(10, sy + 4, buf, w - 190, C_DIM);

    char right[24];
    pos = 0;
    append_str(right, &pos, "Sort: ", sizeof(right));
    append_str(right, &pos, sort_mode == 0 ? "Name" : (sort_mode == 1 ? "Size" : "Type"), sizeof(right));
    append_str(right, &pos, sort_desc ? " v" : " ^", sizeof(right));
    trx(w - 10, sy + 4, right, C_MUTED);
}

static void ctx_rect(int w, int h, int *x, int *y, int *cw, int *ch) {
    *cw = 150;
    *ch = 5 * 26;
    *x = ctx_x;
    *y = ctx_y;
    if (*x + *cw > w) *x = w - *cw;
    if (*y + *ch > h) *y = h - *ch;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
}

static void ctx_sub_rect(int w, int h, int *x, int *y) {
    int cx, cy, cw, ch;
    ctx_rect(w, h, &cx, &cy, &cw, &ch);
    *x = cx + cw;
    *y = cy + 26;
    if (*x + 118 > w) *x = cx - 118;
    if (*y + 3 * 26 > h) *y = h - 3 * 26;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
}

static void draw_ctx(int w, int h) {
    int cx, cy, cw, ch;
    ctx_rect(w, h, &cx, &cy, &cw, &ch);
    fr(cx + 3, cy + 4, cw, ch, 0x66000000);
    frr(cx, cy, cw, ch, 4, C_SURFACE);
    drr(cx, cy, cw, ch, 4, C_STROKE);

    const char *items[5] = {"Open", "Open with  >", "New folder", "Properties", "Delete"};
    for (int i = 0; i < 5; i++) {
        int iy = cy + i * 26 + 5;
        uint32_t col = i == 4 ? C_DANGER : C_DIM;
        if (i == 1) col = C_TEXT;
        tx(cx + 12, iy, items[i], col);
    }

    if (ctx_sub) {
        int sx, sy;
        ctx_sub_rect(w, h, &sx, &sy);
        fr(sx + 3, sy + 4, 118, 3 * 26, 0x66000000);
        frr(sx, sy, 118, 3 * 26, 4, C_SURFACE);
        drr(sx, sy, 118, 3 * 26, 4, C_STROKE);
        tx(sx + 12, sy + 5, "Notes", C_DIM);
        tx(sx + 12, sy + 31, "Editor", C_DIM);
        tx(sx + 12, sy + 57, "Browser", C_DIM);
    }
}

static void modal_rect(int w, int h, int mw, int mh, int *px, int *py) {
    *px = (w - mw) / 2;
    *py = (h - mh) / 2;
    if (*px < 0) *px = 0;
    if (*py < 0) *py = 0;
}

static void modal_panel(int px, int py, int mw, int mh, const char *title) {
    fr(px + 4, py + 5, mw, mh, 0x66000000);
    frr(px, py, mw, mh, 6, C_SURFACE);
    drr(px, py, mw, mh, 6, C_STROKE);
    tx(px + 20, py + 16, title, C_TEXT);
}

static void draw_new_modal(int w, int h) {
    int px, py;
    modal_rect(w, h, 340, 160, &px, &py);
    modal_panel(px, py, 340, 160, "New folder");
    tx(px + 20, py + 46, "Name", C_MUTED);
    frr(px + 20, py + 62, 300, 30, 4, C_BG);
    drr(px + 20, py + 62, 300, 30, 4, C_ACCENT);
    tx(px + 28, py + 69, new_len ? new_name : "Untitled folder", new_len ? C_TEXT : C_MUTED);
    if (((timer_get_ticks() / 20) & 1) == 0)
        fr(px + 28 + new_len * 8, py + 68, 1, 18, C_ACCENT);
    draw_button(px + 20, py + 108, 100, 28, "Create", true);
    draw_button(px + 128, py + 108, 100, 28, "Cancel", false);
}

static void draw_delete_modal(int w, int h) {
    int px, py;
    modal_rect(w, h, 360, 150, &px, &py);
    modal_panel(px, py, 360, 150, "Delete item?");
    char label[SOLFS_MAX_NAME];
    if (selected >= 0) entry_label(selected, label, SOLFS_MAX_NAME);
    else str_cpy(label, "item", sizeof(label));
    txc(px + 20, py + 46, label, 320, C_DIM);
    tx(px + 20, py + 68, "This cannot be undone.", C_MUTED);
    draw_button(px + 20, py + 100, 110, 28, "Delete", true);
    draw_button(px + 140, py + 100, 100, 28, "Cancel", false);
}

static void draw_props_modal(int w, int h) {
    int px, py;
    modal_rect(w, h, 380, 230, &px, &py);
    modal_panel(px, py, 380, 230, "Properties");
    if (selected < 0) return;

    char label[SOLFS_MAX_NAME];
    entry_label(selected, label, SOLFS_MAX_NAME);
    char sz[16];
    if (is_dir(entries[selected])) str_cpy(sz, "-", sizeof(sz));
    else format_size(sizes[selected], sz, sizeof(sz));
    char path[PATH_MAX_S];
    entry_path(selected, path, PATH_MAX_S);

    tx(px + 20, py + 52, "Name", C_MUTED);
    txc(px + 100, py + 52, label, 260, C_DIM);
    tx(px + 20, py + 78, "Type", C_MUTED);
    tx(px + 100, py + 78, type_for(entries[selected]), C_DIM);
    tx(px + 20, py + 104, "Size", C_MUTED);
    tx(px + 100, py + 104, sz, C_DIM);
    tx(px + 20, py + 130, "Location", C_MUTED);
    txc(px + 100, py + 130, path, 260, C_DIM);
    draw_button(px + 140, py + 180, 100, 28, "OK", true);
}

void filebrowser_draw(int x, int y, int w, int h) {
    ox = x;
    oy = y;
    fr(0, 0, w, h, C_BG);

    last_rows = list_rows(h);
    clamp_scroll(last_rows);

    draw_toolbar(w);
    draw_sidebar(h);
    draw_list_header(w);
    draw_rows(w, h);
    if (has_inspector(w)) draw_inspector(w, h);
    draw_status(w, h);

    if (ctx_open) draw_ctx(w, h);
    if (new_open) draw_new_modal(w, h);
    if (delete_open) draw_delete_modal(w, h);
    if (props_open) draw_props_modal(w, h);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static void open_entry_in(int idx, int slot) {
    if (idx < 0 || idx >= entry_count || is_dir(entries[idx])) return;
    char path[PATH_MAX_S];
    entry_path(idx, path, PATH_MAX_S);
    gui_open_with(slot, path);
}

static int pos_of_selected(void) {
    for (int i = 0; i < view_count; i++)
        if (view[i] == selected) return i;
    return -1;
}

static void move_sel(int delta) {
    int pos = pos_of_selected();
    if (pos < 0) pos = delta > 0 ? -1 : 0;
    pos += delta;
    if (pos < 0) pos = 0;
    if (pos >= view_count) pos = view_count - 1;
    if (pos < 0) return;
    select_entry(view[pos]);
    if (pos < scroll) scroll = pos;
    if (pos >= scroll + last_rows) scroll = pos - last_rows + 1;
    clamp_scroll(last_rows);
}

static void open_selected(void) {
    if (selected < 0 && view_count > 0) select_entry(view[0]);
    if (selected >= 0) open_entry(selected);
}

static void ctx_click(int w, int h, int rx, int ry) {
    int cx, cy, cw, ch;
    ctx_rect(w, h, &cx, &cy, &cw, &ch);
    if (ctx_sub) {
        int sx, sy;
        ctx_sub_rect(w, h, &sx, &sy);
        if (rx >= sx && rx < sx + 118 && ry >= sy && ry < sy + 3 * 26) {
            int choice = (ry - sy) / 26;
            int app = choice == 0 ? SPX_NOTEPAD : (choice == 1 ? SPX_EDITOR : SPX_BROWSER);
            int entry = ctx_entry;
            ctx_open = false;
            ctx_sub = false;
            open_entry_in(entry, app);
            return;
        }
    }
    if (rx >= cx && rx < cx + cw && ry >= cy && ry < cy + ch) {
        int action = (ry - cy) / 26;
        if (action == 0) {
            int entry = ctx_entry;
            ctx_open = false;
            ctx_sub = false;
            if (entry >= 0) open_entry(entry);
        } else if (action == 1) {
            ctx_sub = !ctx_sub;
        } else if (action == 2) {
            ctx_open = false;
            ctx_sub = false;
            new_open = true;
            new_len = 0;
            new_name[0] = '\0';
        } else if (action == 3) {
            ctx_open = false;
            ctx_sub = false;
            props_open = true;
        } else if (action == 4) {
            ctx_open = false;
            ctx_sub = false;
            if (ctx_entry >= 0 && !is_dir(entries[ctx_entry])) {
                selected = ctx_entry;
                delete_open = true;
            }
        }
        return;
    }
    ctx_open = false;
    ctx_sub = false;
}

void filebrowser_handle_mouse(int x, int y, int w, int h, int mx, int my) {
    int rx = mx - x;
    int ry = my - y;

    if (new_open) {
        int px, py;
        modal_rect(w, h, 340, 160, &px, &py);
        if (rx >= px + 20 && rx < px + 120 && ry >= py + 108 && ry < py + 136) { create_folder(); return; }
        if (rx >= px + 128 && rx < px + 228 && ry >= py + 108 && ry < py + 136) { new_open = false; return; }
        return;
    }
    if (delete_open) {
        int px, py;
        modal_rect(w, h, 360, 150, &px, &py);
        if (rx >= px + 20 && rx < px + 130 && ry >= py + 100 && ry < py + 128) { delete_selected(); return; }
        if (rx >= px + 140 && rx < px + 240 && ry >= py + 100 && ry < py + 128) { delete_open = false; return; }
        return;
    }
    if (props_open) {
        int px, py;
        modal_rect(w, h, 380, 230, &px, &py);
        if (rx >= px + 140 && rx < px + 240 && ry >= py + 180 && ry < py + 208)
            props_open = false;
        return;
    }
    if (ctx_open) { ctx_click(w, h, rx, ry); return; }

    if (ry < TOOLBAR_H) {
        if (rx >= 8 && rx < 50) { go_up(); return; }
        if (rx >= 54 && rx < 126) { refresh_list(); return; }
        if (rx >= 130 && rx < 194) {
            new_open = true;
            new_len = 0;
            new_name[0] = '\0';
            return;
        }
        if (rx >= 202) {
            search_active = !search_active;
            if (!search_active) {
                query_len = 0;
                query[0] = '\0';
                selected = -1;
                refresh_list();
            }
            return;
        }
        return;
    }

    if (rx < SIDEBAR_W) {
        int by = h - STATUS_H - 42;
        if (by > TOOLBAR_H + 190 && ry >= by && ry < by + 28) {
            new_open = true;
            new_len = 0;
            new_name[0] = '\0';
            return;
        }
        int idx = (ry - (TOOLBAR_H + 34)) / 30;
        if (ry >= TOOLBAR_H + 34 && idx >= 0 && idx < PLACE_COUNT)
            navigate_to(places[idx].path);
        return;
    }

    int lw = content_w(w);
    if (ry >= h - STATUS_H) {
        if (rx > w - 140) set_sort((sort_mode + 1) % 3);
        return;
    }

    if (ry >= TOOLBAR_H && ry < TOOLBAR_H + HEADER_H) {
        if (rx >= SIDEBAR_W && rx < SIDEBAR_W + lw) {
            if (rx < size_right(w) - 56) set_sort(0);
            else if (rx < type_right(w) - 64) set_sort(1);
            else set_sort(2);
        }
        return;
    }

    int rows = list_rows(h);
    int ly = TOOLBAR_H + HEADER_H;
    if (view_count > rows && rx >= SIDEBAR_W + lw - 8 && rx < SIDEBAR_W + lw &&
        ry >= ly && ry < ly + rows * ROW_H) {
        scroll += (ry < ly + rows * ROW_H / 2) ? -rows : rows;
        clamp_scroll(rows);
        return;
    }

    if (rx >= SIDEBAR_W && rx < SIDEBAR_W + lw && ry >= ly && ry < ly + rows * ROW_H) {
        int pos = scroll + (ry - ly) / ROW_H;
        if (pos < 0 || pos >= view_count) { selected = -1; preview_len = 0; return; }
        int idx = view[pos];
        uint32_t now = timer_get_ticks();
        bool dbl = last_click == idx && (now - last_click_tick) <= 40;
        select_entry(idx);
        last_click = idx;
        last_click_tick = now;
        if (dbl) {
            last_click = -1;
            open_entry(idx);
        }
    }
}

void filebrowser_handle_context(int x, int y, int w, int h, int mx, int my) {
    int rx = mx - x;
    int ry = my - y;
    if (new_open || delete_open || props_open) return;
    int rows = list_rows(h);
    int lw = content_w(w);
    int ly = TOOLBAR_H + HEADER_H;
    if (rx < SIDEBAR_W || rx >= SIDEBAR_W + lw || ry < ly || ry >= ly + rows * ROW_H) {
        ctx_open = false;
        ctx_sub = false;
        return;
    }
    int pos = scroll + (ry - ly) / ROW_H;
    if (pos < 0 || pos >= view_count) {
        ctx_open = false;
        ctx_sub = false;
        return;
    }
    selected = view[pos];
    load_preview(selected);
    ctx_entry = selected;
    ctx_x = rx;
    ctx_y = ry;
    ctx_open = true;
    ctx_sub = false;
}

void filebrowser_handle_scroll(int x, int y, int w, int h, int notches) {
    (void)x;
    (void)y;
    (void)w;
    if (new_open || delete_open || props_open || ctx_open) return;
    scroll -= notches * 3;
    clamp_scroll(list_rows(h));
}

void filebrowser_handle_key(char key) {
    if (new_open) {
        if (key == 0x1B) { new_open = false; return; }
        if (key == '\n' || key == '\r') { create_folder(); return; }
        if (key == 0x7F || key == 0x08) {
            if (new_len > 0) new_name[--new_len] = '\0';
            return;
        }
        if (key >= 32 && key < 127 && new_len < SOLFS_MAX_NAME - 1) {
            new_name[new_len++] = key;
            new_name[new_len] = '\0';
        }
        return;
    }
    if (delete_open) {
        if (key == '\n' || key == '\r') delete_selected();
        else if (key == 0x1B) delete_open = false;
        return;
    }
    if (props_open) {
        if (key == 0x1B || key == '\n' || key == '\r') props_open = false;
        return;
    }
    if (ctx_open) {
        if (key == 0x1B) { ctx_open = false; ctx_sub = false; }
        return;
    }

    if (search_active) {
        if (key == 0x1B) {
            search_active = false;
            query_len = 0;
            query[0] = '\0';
            selected = -1;
            refresh_list();
            return;
        }
        if (key == '\n' || key == '\r') { open_selected(); return; }
        if (key == 0x7F || key == 0x08) {
            if (query_len > 0) {
                query[--query_len] = '\0';
                refresh_list();
                if (view_count > 0) select_entry(view[0]);
            }
            return;
        }
        if (key >= 32 && key < 127 && query_len < QUERY_MAX - 1) {
            query[query_len++] = key;
            query[query_len] = '\0';
            refresh_list();
            if (view_count > 0) select_entry(view[0]);
            else { selected = -1; preview_len = 0; }
        }
        return;
    }

    if (key == '/') {
        search_active = true;
        query_len = 0;
        query[0] = '\0';
        return;
    }
    if (key == 'j') { move_sel(1); return; }
    if (key == 'k') { move_sel(-1); return; }
    if (key == '\n' || key == '\r') { open_selected(); return; }
    if (key == 0x08 || key == 'h') { go_up(); return; }
    if (key == 0x7F) {
        if (selected >= 0 && !is_dir(entries[selected])) delete_open = true;
        return;
    }
    if (key == 'r' || key == 'R') { refresh_list(); return; }
    if (key == 's' || key == 'S') { set_sort((sort_mode + 1) % 3); return; }
    if (key == 'n' || key == 'N') {
        new_open = true;
        new_len = 0;
        new_name[0] = '\0';
        return;
    }
    if (key == 0x1B) {
        if (selected >= 0) { selected = -1; preview_len = 0; }
        else go_up();
    }
}

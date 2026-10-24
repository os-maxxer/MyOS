#include <solis/apps/filebrowser.h>
#include <solis/graphics.h>
#include <solis/vfs.h>
#include <solis/gui.h>
#include <solis/spx.h>
#include <stdbool.h>

#define SIDEBAR_W 150
#define MAX_VISIBLE 20
#define HEADER_H 24
#define ITEM_H 22
#define PATH_MAX 64

static char current_path[PATH_MAX];
static char entries[SOLFS_MAX_FILES][SOLFS_MAX_NAME];
static int  entry_count = 0;
static int  selected = -1;
static int  scroll_offset = 0;
static char preview_buf[1024];
static int  preview_len = 0;
static bool context_open = false;
static bool context_submenu = false;
static bool properties_open = false;
static int context_x, context_y, context_entry = -1;

static int str_len(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static bool str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void str_cpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool is_dir(const char *name) {
    int n = str_len(name);
    return n > 0 && name[n - 1] == '/';
}

void filebrowser_init(void) {
    str_cpy(current_path, "/home", PATH_MAX);
    selected = -1;
    scroll_offset = 0;
    preview_len = 0;
    context_open = false;
    context_submenu = false;
    properties_open = false;
}

static void refresh_list(void) {
    entry_count = vfs_ls_at(current_path, entries, SOLFS_MAX_FILES);
    if (selected >= entry_count) selected = -1;
}

static void load_preview(int idx) {
    if (idx < 0 || idx >= entry_count || is_dir(entries[idx])) {
        preview_len = 0;
        return;
    }
    char full[PATH_MAX];
    str_cpy(full, current_path, PATH_MAX);
    int fl = str_len(full);
    if (fl > 0 && full[fl - 1] != '/') {
        full[fl++] = '/';
        full[fl] = '\0';
    }
    str_cpy(full + fl, entries[idx], PATH_MAX - fl);

    int fd = vfs_open(full);
    if (fd < 0) { preview_len = 0; return; }
    int sz = vfs_get_size(fd);
    if (sz > 1024) sz = 1024;
    preview_len = vfs_read(fd, (uint8_t*)preview_buf, sz);
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

static void select_entry(int idx) {
    if (idx >= 0 && idx < entry_count) {
        selected = idx;
        load_preview(idx);
    }
}

static void navigate_to(const char *path) {
    str_cpy(current_path, path, PATH_MAX);
    scroll_offset = 0;
    selected = -1;
    preview_len = 0;
    refresh_list();
}

static void go_up(void) {
    int len = str_len(current_path);
    if (len <= 1) return;
    int i = len - 1;
    while (i > 0 && current_path[i] != '/') i--;
    if (i == 0) {
        navigate_to("/");
    } else {
        current_path[i] = '\0';
        navigate_to(current_path);
    }
}

static bool nav_to_subdir(const char *name) {
    int pl = str_len(current_path);
    char full[PATH_MAX];
    str_cpy(full, current_path, PATH_MAX);
    if (pl > 0 && current_path[pl - 1] != '/') {
        int fl = str_len(full);
        full[fl++] = '/';
        full[fl] = '\0';
    }
    int fl = str_len(full);
    str_cpy(full + fl, name, PATH_MAX - fl);
    int nl = str_len(full);
    if (nl > 0 && full[nl - 1] == '/')
        full[nl - 1] = '\0';
    navigate_to(full);
    return true;
}

static int entry_size(int idx) {
    if (idx < 0 || idx >= entry_count || is_dir(entries[idx])) return 0;
    char full[PATH_MAX];
    str_cpy(full, current_path, PATH_MAX);
    int fl = str_len(full);
    if (fl > 0 && full[fl - 1] != '/') {
        full[fl++] = '/';
        full[fl] = '\0';
    }
    str_cpy(full + fl, entries[idx], PATH_MAX - fl);
    int fd = vfs_open(full);
    if (fd < 0) return 0;
    int sz = vfs_get_size(fd);
    return sz;
}

static void entry_path(int idx, char *out, int max) {
    str_cpy(out, current_path, max);
    int n = str_len(out);
    if (n > 0 && out[n - 1] != '/' && n < max - 1) out[n++] = '/';
    out[n] = '\0';
    str_cpy(out + n, entries[idx], max - n);
    int len = str_len(out);
    if (len > 0 && out[len - 1] == '/') out[len - 1] = '\0';
}

static int default_app_for_file(const char *name) {
    int len = str_len(name);
    if (len >= 2 && name[len - 2] == '.' &&
        (name[len - 1] == 'c' || name[len - 1] == 'h')) return SPX_EDITOR;
    if (len >= 4 && name[len - 4] == '.' && name[len - 3] == 'l' &&
        name[len - 2] == 'u' && name[len - 1] == 'a') return SPX_EDITOR;
    return SPX_NOTEPAD;
}

static void open_entry_in(int idx, int app_slot) {
    if (idx < 0 || idx >= entry_count || is_dir(entries[idx])) return;
    char path[PATH_MAX];
    entry_path(idx, path, sizeof(path));
    gui_open_with(app_slot, path);
}

static void draw_context_menu(int x, int y, int w, int h) {
    if (properties_open && context_entry >= 0 && context_entry < entry_count) {
        int px = x + (w - 250) / 2, py = y + (h - 110) / 2;
        graphics_fill_rect(px + 3, py + 4, 250, 110, 0x88000000);
        graphics_fill_rect(px, py, 250, 110, 0xFF171E4B);
        graphics_draw_rect(px, py, 250, 110, 0xFF596DE8);
        graphics_draw_string(px + 10, py + 8, "File properties", 0xFFFFFFFF);
        graphics_draw_string(px + 10, py + 32, entries[context_entry], 0xFFEAF3FF);
        char size[12]; int_to_str(entry_size(context_entry), size);
        graphics_draw_string(px + 10, py + 56, "Size:", 0xFFAAA6B5);
        graphics_draw_string(px + 58, py + 56, size, 0xFFEAF3FF);
        graphics_draw_string(px + 10, py + 80, current_path, 0xFFAAA6B5);
        return;
    }
    if (!context_open) return;
    const int mw = 126, row_h = 22;
    int mx = context_x, my = context_y;
    if (mx + mw > w) mx = w - mw;
    if (my + row_h * 3 > h) my = h - row_h * 3;
    graphics_fill_rect(x + mx + 3, y + my + 4, mw, row_h * 3, 0x77000000);
    graphics_fill_rect(x + mx, y + my, mw, row_h * 3, 0xFF171E4B);
    graphics_draw_rect(x + mx, y + my, mw, row_h * 3, 0xFF596DE8);
    graphics_draw_string(x + mx + 8, y + my + 3, "Open", 0xFFFFFFFF);
    graphics_draw_string(x + mx + 8, y + my + row_h + 3, "Open with  >", 0xFFEAF3FF);
    graphics_draw_string(x + mx + 8, y + my + row_h * 2 + 3, "Properties", 0xFFEAF3FF);
    if (context_submenu) {
        int sx = mx + mw, sy = my + row_h;
        if (sx + 116 > w) sx = mx - 116;
        if (sy + row_h * 2 > h) sy = h - row_h * 2;
        graphics_fill_rect(x + sx, y + sy, 116, row_h * 2, 0xFF171E4B);
        graphics_draw_rect(x + sx, y + sy, 116, row_h * 2, 0xFF596DE8);
        graphics_draw_string(x + sx + 8, y + sy + 3, "Notes", 0xFFEAF3FF);
        graphics_draw_string(x + sx + 8, y + sy + row_h + 3, "Editor", 0xFFEAF3FF);
    }
}

void filebrowser_draw(int x, int y, int w, int h) {
    graphics_fill_rect(x, y, w, h, 0xFFF0F0F0);

    // Sidebar
    graphics_fill_rect(x, y, SIDEBAR_W, h, 0xFF2D2D2D);
    graphics_draw_rect(x, y, SIDEBAR_W, h, 0xFF444444);
    graphics_draw_string(x + 8, y + 6, "Places", 0xFFAAAAAA);

    struct { const char *label; const char *path; } places[] = {
        {"C:/",        "/"},
        {"Home",       "/home"},
        {"Documents",  "/docs"},
        {"Downloads",  "/downloads"},
        {"Desktop",    "/desktop"},
    };
    int nplaces = 5;
    for (int i = 0; i < nplaces; i++) {
        int iy = y + 28 + i * 28;
        bool active = str_eq(current_path, places[i].path);
        graphics_fill_rect(x + 4, iy, SIDEBAR_W - 8, 26, active ? 0xFF4A90E2 : 0xFF3D3D3D);
        graphics_draw_string(x + 14, iy + 6, places[i].label, active ? 0xFFFFFFFF : 0xFFCCCCCC);
    }

    // Main area
    int main_x = x + SIDEBAR_W;
    int main_w = w - SIDEBAR_W;

    // Header
    graphics_fill_rect(main_x, y, main_w, HEADER_H, 0xFFE0E0E0);
    graphics_draw_rect(main_x, y, main_w, HEADER_H, 0xFFCCCCCC);
    graphics_draw_string(main_x + 8, y + 5, "Name", 0xFF333333);
    graphics_draw_string(main_x + main_w - 80, y + 5, "Size", 0xFF333333);

    refresh_list();

    // File list
    int list_y = y + HEADER_H;
    int list_h = h - HEADER_H - 22;
    int visible = list_h / ITEM_H;
    if (visible > MAX_VISIBLE) visible = MAX_VISIBLE;

    if (scroll_offset > entry_count - visible && visible < entry_count)
        scroll_offset = entry_count - visible;
    if (scroll_offset < 0) scroll_offset = 0;

    for (int i = 0; i < visible; i++) {
        int idx = scroll_offset + i;
        if (idx >= entry_count) break;

        int iy = list_y + i * ITEM_H;
        uint32_t bg = (idx == selected) ? 0xFF4A90E2 : ((i & 1) ? 0xFFFFFFFF : 0xFFF5F5F5);
        uint32_t fg = (idx == selected) ? 0xFFFFFFFF : 0xFF333333;
        graphics_fill_rect(main_x, iy, main_w, ITEM_H, bg);

        const char *name = entries[idx];
        int nl = str_len(name);
        bool dirent = nl > 0 && name[nl - 1] == '/';

        // Folder icon or file indicator
        if (dirent) {
            graphics_draw_string(main_x + 6, iy + 4, ">", fg);
            graphics_draw_string(main_x + 16, iy + 4, name, fg);
        } else {
            graphics_draw_string(main_x + 8, iy + 4, name, fg);
            char sz_str[12];
            int_to_str(entry_size(idx), sz_str);
            graphics_draw_string(main_x + main_w - 80, iy + 4, sz_str, fg);
        }
    }

    // Preview pane
    if (selected >= 0 && selected < entry_count && !is_dir(entries[selected])) {
        int preview_y = list_y + visible * ITEM_H + 2;
        int preview_h = list_h - visible * ITEM_H - 2;
        if (preview_h > 60) preview_h = 60;

        graphics_fill_rect(main_x + 2, preview_y, main_w - 4, preview_h, 0xFFFFFDE0);
        graphics_draw_rect(main_x + 2, preview_y, main_w - 4, preview_h, 0xFFDDDCC8);

        graphics_draw_string(main_x + 8, preview_y + 3, entries[selected], 0xFF222222);

        char sz_line[32];
        int si = 0;
        const char *sp = "Size: ";
        while (*sp) sz_line[si++] = *sp++;
        char sz_str[12];
        int_to_str(entry_size(selected), sz_str);
        int si3 = 0;
        while (sz_str[si3]) sz_line[si++] = sz_str[si3++];
        sz_line[si] = '\0';
        graphics_draw_string(main_x + 8, preview_y + 18, sz_line, 0xFF666666);

        int cols = (main_w - 24) / 8;
        if (cols > 64) cols = 64;
        int rows = (preview_h - 40) / 16;
        int pos = 0;
        for (int r = 0; r < rows && pos < preview_len; r++) {
            for (int c = 0; c < cols && pos < preview_len; c++) {
                char ch = preview_buf[pos++];
                if (ch >= 32 && ch < 127) {
                    char str[2] = {ch, '\0'};
                    graphics_draw_string(main_x + 8 + c * 8, preview_y + 36 + r * 16, str, 0xFF333333);
                }
            }
        }
    }

    // Status bar
    int sb_y = y + h - 20;
    graphics_fill_rect(x, sb_y, w, 20, 0xFF2D2D2D);
    char status[48];
    int si = 0;
    const char *drive = "c:";
    while (*drive) status[si++] = *drive++;
    const char *rest = current_path;
    if (!rest[0]) rest = "/";
    while (*rest && si < 40) status[si++] = *rest++;
    status[si++] = ' ';
    status[si++] = '-';
    status[si++] = ' ';
    int_to_str(entry_count, status + si);
    while (status[si]) si++;
    status[si] = '\0';
    graphics_draw_string(x + 6, sb_y + 3, status, 0xFFCCCCCC);
    draw_context_menu(x, y, w, h);
}

void filebrowser_handle_key(char key) {
    if (key == 'k') {
        if (selected > 0) {
            select_entry(selected - 1);
            if (selected < scroll_offset) scroll_offset = selected;
        }
    } else if (key == 'j') {
        if (selected < entry_count - 1) {
            select_entry(selected + 1);
            if (selected >= scroll_offset + 15) scroll_offset = selected - 15 + 1;
        }
    } else if (key == 'h' || key == 0x1B) {
        go_up();
    } else if (key == '\n' || key == '\r') {
        if (selected >= 0 && selected < entry_count && is_dir(entries[selected])) {
            nav_to_subdir(entries[selected]);
        }
    }
}

void filebrowser_handle_mouse(int x, int y, int w, int h, int mouse_x, int mouse_y) {
    int rel_x = mouse_x - x;
    int rel_y = mouse_y - y;

    if (properties_open) {
        properties_open = false;
        context_open = false;
        return;
    }
    if (context_open) {
        const int mw = 126, row_h = 22;
        int mx = context_x, my = context_y;
        if (mx + mw > w) mx = w - mw;
        if (my + row_h * 3 > h) my = h - row_h * 3;
        if (context_submenu) {
            int sx = mx + mw, sy = my + row_h;
            if (sx + 116 > w) sx = mx - 116;
            if (sy + row_h * 2 > h) sy = h - row_h * 2;
            if (rel_x >= sx && rel_x < sx + 116 && rel_y >= sy && rel_y < sy + row_h * 2) {
                int choice = (rel_y - sy) / row_h;
                context_open = false;
                context_submenu = false;
                open_entry_in(context_entry, choice == 0 ? SPX_NOTEPAD : SPX_EDITOR);
                return;
            }
        }
        if (rel_x >= mx && rel_x < mx + mw && rel_y >= my && rel_y < my + row_h * 3) {
            int action = (rel_y - my) / row_h;
            if (action == 0 && context_entry >= 0 && context_entry < entry_count) {
                if (is_dir(entries[context_entry])) nav_to_subdir(entries[context_entry]);
                else open_entry_in(context_entry, default_app_for_file(entries[context_entry]));
                context_open = false;
            } else if (action == 1 && context_entry >= 0 && !is_dir(entries[context_entry])) {
                context_submenu = !context_submenu;
            } else if (action == 2) {
                properties_open = true;
                context_open = false;
            }
            return;
        }
        context_open = false;
        context_submenu = false;
    }

    // Sidebar clicks
    if (rel_x >= 0 && rel_x < SIDEBAR_W && rel_y >= 28) {
        int idx = (rel_y - 28) / 28;
        struct { const char *label; const char *path; } places[] = {
            {"C:/",        "/"},
            {"Home",       "/home"},
            {"Documents",  "/docs"},
            {"Downloads",  "/downloads"},
            {"Desktop",    "/desktop"},
        };
        if (idx >= 0 && idx < 5) {
            navigate_to(places[idx].path);
        }
        return;
    }

    // Main area clicks
    int main_x = SIDEBAR_W;
    int main_w = w - SIDEBAR_W;
    int list_y = HEADER_H;
    int list_h = h - HEADER_H - 22;
    int visible = list_h / ITEM_H;
    if (visible > MAX_VISIBLE) visible = MAX_VISIBLE;

    int mrel_x = mouse_x - (x + main_x);
    int mrel_y = mouse_y - (y + list_y);

    if (mrel_x >= 0 && mrel_x < main_w && mrel_y >= 0) {
        int idx = scroll_offset + mrel_y / ITEM_H;
        if (idx >= 0 && idx < entry_count) {
            select_entry(idx);
            // Double-click or click on dir: navigate
            if (is_dir(entries[idx])) {
                nav_to_subdir(entries[idx]);
            }
        }
    }
}

void filebrowser_handle_context(int x, int y, int w, int h, int mouse_x, int mouse_y) {
    int rel_x = mouse_x - x;
    int rel_y = mouse_y - y;
    int main_x = SIDEBAR_W;
    int main_w = w - SIDEBAR_W;
    int list_y = HEADER_H;
    int list_h = h - HEADER_H - 22;
    int visible = list_h / ITEM_H;
    if (visible > MAX_VISIBLE) visible = MAX_VISIBLE;
    if (rel_x < main_x || rel_x >= main_x + main_w || rel_y < list_y ||
        rel_y >= list_y + visible * ITEM_H) {
        context_open = false;
        properties_open = false;
        return;
    }
    int idx = scroll_offset + (rel_y - list_y) / ITEM_H;
    if (idx < 0 || idx >= entry_count) return;
    select_entry(idx);
    context_entry = idx;
    context_x = rel_x;
    context_y = rel_y;
    context_open = true;
    context_submenu = false;
    properties_open = false;
}

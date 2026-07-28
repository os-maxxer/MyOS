#include <nyx/graphics.h>
#include <nyx/timer.h>
#include <nyx/npx.h>
#include <stdbool.h>

#define LINE_H 16
#define COLS 60

static char lines[32][COLS];
static int line_count;
static bool dirty;

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

static void uint64_to_hex(uint64_t n, char *buf) {
    const char *hex = "0123456789ABCDEF";
    int bi = 0;
    for (int i = 60; i >= 0; i -= 4) {
        char c = hex[(n >> i) & 0xF];
        if (c != '0' || bi > 0 || i == 0) {
            buf[bi++] = c;
        }
    }
    if (bi == 0) { buf[bi++] = '0'; }
    buf[bi] = '\0';
}

static void uptime_str(char *buf) {
    uint32_t ticks = timer_get_ticks();
    uint32_t secs = ticks / 100;
    uint32_t mins = secs / 60;
    uint32_t hrs = mins / 60;
    secs %= 60;
    mins %= 60;
    int bi = 0;
    if (hrs > 0) {
        char tmp[12];
        int ti = 0;
        uint32_t n = hrs;
        while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
        while (ti > 0) buf[bi++] = tmp[--ti];
        buf[bi++] = 'h';
    }
    {
        char tmp[12];
        int ti = 0;
        uint32_t n = mins;
        while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
        if (ti == 0) tmp[ti++] = '0';
        while (ti > 0) buf[bi++] = tmp[--ti];
    }
    buf[bi++] = 'm';
    {
        char tmp[12];
        int ti = 0;
        uint32_t n = secs;
        while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
        if (ti == 0) tmp[ti++] = '0';
        while (ti > 0) buf[bi++] = tmp[--ti];
    }
    buf[bi++] = 's';
    buf[bi] = '\0';
}

void taskman_init(void) {
    clear_screen();
    dirty = true;
}

void taskman_draw(int x, int y, int w, int h) {
    if (!dirty) return;
    dirty = false;

    clear_screen();

    char buf[64];
    int bi;

    add_line("=== Task Manager ===");
    add_line("--------------------");

    char cpu[48] = {0};
    sys_get_cpu_brand(cpu, 48);
    bi = 0;
    {
        const char *p = "CPU: ";
        while (*p) buf[bi++] = *p++;
        const char *s = cpu;
        while (*s && bi < 60) buf[bi++] = *s++;
    }
    buf[bi] = '\0';
    add_line(buf);

    uint32_t ram = sys_get_total_ram();
    char ram_str[16];
    uint32_to_str(ram, ram_str);
    bi = 0;
    {
        const char *p = "RAM: ";
        while (*p) buf[bi++] = *p++;
        const char *s = ram_str;
        while (*s && bi < 60) buf[bi++] = *s++;
        const char *p2 = " MB";
        while (*p2 && bi < 60) buf[bi++] = *p2++;
    }
    buf[bi] = '\0';
    add_line(buf);

    char up[32];
    uptime_str(up);
    bi = 0;
    {
        const char *p = "Uptime: ";
        while (*p) buf[bi++] = *p++;
        const char *s = up;
        while (*s && bi < 60) buf[bi++] = *s++;
    }
    buf[bi] = '\0';
    add_line(buf);

    uint64_t mid = sys_get_machine_id();
    char mid_str[20];
    uint64_to_hex(mid, mid_str);
    bi = 0;
    {
        const char *p = "Machine ID: 0x";
        while (*p) buf[bi++] = *p++;
        const char *s = mid_str;
        while (*s && bi < 60) buf[bi++] = *s++;
    }
    buf[bi] = '\0';
    add_line(buf);

    add_line("--------------------");
    add_line("Installed Apps:");

    char app_names[NPX_MAX_APPS][NPX_NAME_LEN];
    int count = npx_list_installed(app_names, NPX_MAX_APPS);
    if (count == 0) {
        add_line("  (none)");
    } else {
        for (int i = 0; i < count; i++) {
            bi = 0;
            buf[bi++] = ' ';
            buf[bi++] = ' ';
            const char *s = app_names[i];
            while (*s && bi < 60) buf[bi++] = *s++;
            buf[bi] = '\0';
            add_line(buf);
        }
    }

    add_line("");
    add_line("Press ESC to close");

    int rows = (h - 8) / LINE_H;
    if (rows > line_count) rows = line_count;

    graphics_fill_rect(x, y, w, h, 0xFF1E1E2E);

    for (int r = 0; r < rows; r++) {
        int src_line = r;
        if (src_line >= line_count) break;
        graphics_draw_string(x + 4, y + 4 + r * LINE_H, lines[src_line], 0xFF00FF00);
    }
}

void taskman_handle_key(char key) {
    if (key == 0x1b) return;
    dirty = true;
}

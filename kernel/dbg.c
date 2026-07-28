#include <nyx/dbg.h>
#include <nyx/ports.h>

#define DBG_BUF_SIZE 4096
static char dbg_buf[DBG_BUF_SIZE];
static int dbg_pos = 0;
static int serial_ok = 0;

static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
    serial_ok = 1;
}

static void serial_putc(char c) {
    if (!serial_ok) return;
    while (!(inb(0x3F8 + 5) & 0x20));
    outb(0x3F8, c);
}

static void serial_puts(const char *s) {
    if (!serial_ok) return;
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void dbg_init(void) {
    serial_init();
    dbg_pos = 0;
    dbg_buf[0] = '\0';
    serial_puts("[DBG] Debug log initialized\n");
}

static void dbg_putchar(char c) {
    if (dbg_pos < DBG_BUF_SIZE - 1) {
        dbg_buf[dbg_pos++] = c;
        dbg_buf[dbg_pos] = '\0';
    }
}

void dbg_print(const char *s) {
    serial_puts(s);
    while (*s) {
        dbg_putchar(*s);
        s++;
    }
}

void dbg_print_hex(uint32_t val) {
    char hex[11];
    int pos = 2;
    hex[0] = '0';
    hex[1] = 'x';
    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0x0F;
        if (nibble || started || i == 0) {
            hex[pos++] = "0123456789ABCDEF"[nibble];
            started = 1;
        }
    }
    hex[pos] = '\0';
    dbg_print(hex);
}

void dbg_print_dec(uint32_t val) {
    char tmp[12];
    int ti = 0;
    if (val == 0) { tmp[ti++] = '0'; }
    while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; }
    char buf[16];
    int bi = 0;
    while (ti > 0) buf[bi++] = tmp[--ti];
    buf[bi] = '\0';
    dbg_print(buf);
}

int dbg_read(char *buf, int max) {
    int i = 0;
    while (i < max - 1 && i < dbg_pos) {
        buf[i] = dbg_buf[i];
        i++;
    }
    buf[i] = '\0';
    return i;
}

#include <solis/dbg.h>
#include <solis/ports.h>
#include <solis/timer.h>

#define DBG_BUF_SIZE 4096
static char dbg_buf[DBG_BUF_SIZE];
static int dbg_pos = 0;
static int serial_ok = 0;
static struct dbg_health dbg_health;

extern char __text_start;
extern char __text_end;
extern char __rodata_start;
extern char __rodata_end;

static uint32_t crc32_update(uint32_t crc, uint8_t data) {
    crc ^= data;
    for (int bit = 0; bit < 8; bit++) {
        if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320u;
        else crc >>= 1;
    }
    return crc;
}

static uint32_t crc32_region(const void *base, uint32_t size) {
    const uint8_t *ptr = (const uint8_t *)base;
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++) {
        crc = crc32_update(crc, ptr[i]);
    }
    return crc ^ 0xFFFFFFFFu;
}

static void dbg_health_default(void) {
    dbg_health.total_irqs = 0;
    dbg_health.memory_used_kb = 0;
    dbg_health.memory_total_kb = 0;
    dbg_health.driver_count = 0;
    dbg_health.pipeline[0] = '\0';
    for (int i = 0; i < DBG_MAX_IRQS; i++) dbg_health.irq_counts[i] = 0;
    for (int i = 0; i < DBG_MAX_DRIVERS; i++) {
        dbg_health.drivers[i].name[0] = '\0';
        dbg_health.drivers[i].state = DBG_DRIVER_UNINITIALIZED;
        dbg_health.drivers[i].last_tick = 0;
    }
}

static void dbg_copy_string(char *dst, int max_len, const char *src) {
    int i = 0;
    while (src && src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int dbg_match_name(const char *a, const char *b) {
    int i = 0;
    while (a && b && a[i] && b[i] && a[i] == b[i]) i++;
    if (a && a[i] == '\0' && b[i] == '\0') return 0;
    if (a && b && a[i] == b[i]) return 0;
    if (a && a[i] == '\0') return -1;
    if (b && b[i] == '\0') return 1;
    return a[i] - b[i];
}

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
    dbg_health_default();
    dbg_health.text_crc = crc32_region(&__text_start, (uint32_t)(&__text_end - &__text_start));
    dbg_health.rodata_crc = crc32_region(&__rodata_start, (uint32_t)(&__rodata_end - &__rodata_start));
    dbg_set_pipeline_stage("boot");
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

void dbg_set_driver_state(const char *name, uint8_t state) {
    if (!name) return;
    for (int i = 0; i < DBG_MAX_DRIVERS; i++) {
        if (dbg_health.drivers[i].name[0] == '\0' || dbg_match_name(dbg_health.drivers[i].name, name) == 0) {
            dbg_copy_string(dbg_health.drivers[i].name, DBG_DRIVER_NAME_LEN, name);
            dbg_health.drivers[i].state = state;
            dbg_health.drivers[i].last_tick = timer_get_ticks();
            if (dbg_health.driver_count < DBG_MAX_DRIVERS && dbg_health.drivers[i].name[0] != '\0' &&
                i == dbg_health.driver_count) {
                dbg_health.driver_count++;
            }
            return;
        }
    }
}

void dbg_set_pipeline_stage(const char *stage) {
    dbg_copy_string(dbg_health.pipeline, DBG_PIPELINE_LEN, stage ? stage : "idle");
}

void dbg_note_irq(uint8_t irq) {
    if (irq >= DBG_MAX_IRQS) return;
    dbg_health.irq_counts[irq]++;
    dbg_health.total_irqs++;
}

void dbg_set_memory_metrics(uint32_t used_kb, uint32_t total_kb) {
    dbg_health.memory_used_kb = used_kb;
    dbg_health.memory_total_kb = total_kb;
}

void dbg_get_health(struct dbg_health *out) {
    if (!out) return;
    *out = dbg_health;
    out->text_crc = crc32_region(&__text_start, (uint32_t)(&__text_end - &__text_start));
    out->rodata_crc = crc32_region(&__rodata_start, (uint32_t)(&__rodata_end - &__rodata_start));
}

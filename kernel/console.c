/*
 * Text-mode console implementation.
 * This provides a simple VGA text console for milestone 1 output.
 */

#include <solis/console.h>
#include <solis/ports.h>

#define COM1 0x3F8

/*
 * Freestanding memory primitives. GCC lowers aggregate assignments and
 * initialisations to these even under -ffreestanding, and the kernel links
 * with -nostdlib, so they have to be provided here.
 */
void *memcpy(void *dst, const void *src, unsigned long n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned long n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int value, unsigned long n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)value;
    return dst;
}

int memcmp(const void *a, const void *b, unsigned long n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (unsigned long i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

static uint16_t *video_memory = (uint16_t *)0xB8000;
static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;
static uint8_t current_color = 0x0F;

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, c);
}

static void console_scroll(void) {
    for (uint8_t row = 1; row < 25; ++row) {
        for (uint8_t col = 0; col < 80; ++col) {
            video_memory[(row - 1) * 80 + col] = video_memory[row * 80 + col];
        }
    }
    for (uint8_t col = 0; col < 80; ++col) {
        video_memory[(24 * 80) + col] = (uint16_t)(' ' | (current_color << 8));
    }
}

void console_init(void) {
    serial_init();
    console_clear();
    console_set_color(0x0F, 0x00);
}

void console_clear(void) {
    for (uint16_t i = 0; i < 80 * 25; ++i) {
        video_memory[i] = (uint16_t)(' ' | (current_color << 8));
    }
    cursor_row = 0;
    cursor_col = 0;
}

void console_set_color(uint8_t foreground, uint8_t background) {
    current_color = (background << 4) | foreground;
}

static void console_newline(void) {
    cursor_col = 0;
    if (cursor_row + 1 >= 25) {
        console_scroll();
    } else {
        cursor_row++;
    }
}

void console_putc(char c) {
    serial_putc(c);
    if (c == '\n') {
        console_newline();
        return;
    }
    if (c == '\r') {
        cursor_col = 0;
        return;
    }
    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        }
        return;
    }
    if (cursor_col >= 80) {
        console_newline();
    }
    uint16_t index = cursor_row * 80 + cursor_col;
    video_memory[index] = (uint16_t)(c | (current_color << 8));
    cursor_col++;
}

void console_write(const char *text) {
    for (const char *p = text; *p != '\0'; ++p) {
        console_putc(*p);
    }
}

void console_write_hex(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buffer[9];
    for (int i = 7; i >= 0; --i) {
        buffer[i] = hex[value & 0xF];
        value >>= 4;
    }
    buffer[8] = '\0';
    console_write(buffer);
}

void console_write_dec(uint32_t value) {
    char buffer[11];
    int index = 10;
    buffer[index] = '\0';
    do {
        buffer[--index] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    console_write(&buffer[index]);
}

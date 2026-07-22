/*
 * Text-mode console implementation.
 * This provides a simple VGA text console for milestone 1 output.
 */

#include <nyx/console.h>

static uint16_t *video_memory = (uint16_t *)0xB8000;
static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;
static uint8_t current_color = 0x0F;

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

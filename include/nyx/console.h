#ifndef NYX_CONSOLE_H
#define NYX_CONSOLE_H

#include <nyx/types.h>

void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char *text);
void console_write_hex(uint32_t value);
void console_write_dec(uint32_t value);
void console_set_color(uint8_t foreground, uint8_t background);

#endif

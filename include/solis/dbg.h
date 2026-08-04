#ifndef SOLIS_DBG_H
#define SOLIS_DBG_H

#include <solis/types.h>

void dbg_init(void);
void dbg_print(const char *s);
void dbg_print_hex(uint32_t val);
void dbg_print_dec(uint32_t val);
int  dbg_read(char *buf, int max);

#endif

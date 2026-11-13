#ifndef SOLIS_APPS_CCOMPILER_H
#define SOLIS_APPS_CCOMPILER_H

#include <solis/jit.h>

/* Compile source into a native SCX image.
 *
 * The compiler accepts a deliberately small but real C subset: int/char,
 * pointers used as opaque addresses, functions with up to six integer or
 * pointer parameters, locals, globals, if/else, while, for, break, continue,
 * return, arithmetic, comparisons, assignments, ++/--, string literals and
 * printf/puts/putchar calls.  It emits x86-64 machine code and a separate
 * data section.
 *
 * On success returns 0 and fills image/image_size.  On failure returns -1;
 * cc_last_error() describes the first error.
 */
int cc_compile(const char *source, uint8_t *image, uint32_t image_capacity,
               uint32_t *image_size);
const char *cc_last_error(void);

/* Runtime entry points called by generated code.  They live in the terminal
 * SPX slot, whose link address is fixed, and intentionally use the SysV ABI. */
void cc_rt_putchar(int c);
void cc_rt_puts(const char *text);
int cc_rt_printf(const char *format, int a, int b, int c, int d, int e);

#endif

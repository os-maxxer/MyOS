#ifndef MYOS_KEYBOARD_H
#define MYOS_KEYBOARD_H

#include <stdbool.h>
#include <myos/types.h>

void keyboard_init(void);
bool keyboard_has_input(void);
bool keyboard_read_char(char *out);

#endif

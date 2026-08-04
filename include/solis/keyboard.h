#ifndef SOLIS_KEYBOARD_H
#define SOLIS_KEYBOARD_H

#include <stdbool.h>
#include <solis/types.h>

void keyboard_init(void);
bool keyboard_has_input(void);
bool keyboard_read_char(char *out);
bool keyboard_is_shift_pressed(void);
bool keyboard_is_ctrl_pressed(void);
bool keyboard_is_alt_pressed(void);
bool keyboard_was_start_menu_pressed(void);
int  keyboard_consume_func_key(void);

#endif

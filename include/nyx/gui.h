#ifndef NYX_GUI_H
#define NYX_GUI_H

#include <nyx/types.h>
#include <stdbool.h>

#define GUI_THEME_SOLID     0
#define GUI_THEME_STARFIELD 1

struct gui_point { int32_t x; int32_t y; };
struct gui_rect { int32_t x; int32_t y; int32_t width; int32_t height; };

void gui_init(void);
void gui_redraw(void);
void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons);
void gui_handle_key(char key);
bool gui_needs_redraw(void);
void gui_close_window(int idx);
void gui_toggle_start_menu(void);
void gui_set_bg_color(uint32_t color);
uint32_t gui_get_bg_color(void);
void gui_set_theme(int theme);
int  gui_get_theme(void);
int  gui_launch_filebrowser(void);

#endif

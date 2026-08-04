#ifndef SOLIS_GUI_H
#define SOLIS_GUI_H

#include <solis/types.h>
#include <stdbool.h>

#define GUI_THEME_SOLID     0
#define GUI_THEME_STARFIELD 1
#define GUI_THEME_SUNSET    2
#define GUI_THEME_CHERRY_BLOSSOM 3
#define GUI_THEME_GNOME     4

struct gui_point { int32_t x; int32_t y; };
struct gui_rect { int32_t x; int32_t y; int32_t width; int32_t height; };

void gui_init(void);
void gui_redraw(void);
void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons);
void gui_handle_key(char key);
bool gui_needs_redraw(void);
void gui_update_clock(void);
void gui_close_window(int idx);
void gui_toggle_start_menu(void);
bool gui_take_logout(void);
void gui_set_bg_color(uint32_t color);
uint32_t gui_get_bg_color(void);
void gui_set_theme(int theme);
int  gui_get_theme(void);
int  gui_launch_filebrowser(void);
int  gui_launch_taskmanager(void);
int  gui_launch_pkg(void);
int  gui_launch_editor(void);
int  gui_launch_tetris(void);
int  gui_launch_app(int slot);

#endif

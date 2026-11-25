#ifndef SOLIS_GUI_H
#define SOLIS_GUI_H

#include <solis/types.h>
#include <stdbool.h>

#define GUI_THEME_SOLID     0
#define GUI_THEME_STARFIELD 1
#define GUI_THEME_SUNSET    2
#define GUI_THEME_CHERRY_BLOSSOM 3
#define GUI_THEME_GNOME     4
#define GUI_THEME_VOXEL     5
#define GUI_THEME_BORED     6
#define GUI_THEME_AURORA    7
#define GUI_THEME_SOLIS_UI_ENGINE 8

struct gui_point { int32_t x; int32_t y; };
struct gui_rect { int32_t x; int32_t y; int32_t width; int32_t height; };

void gui_init(void);
void gui_redraw(void);
void gui_handle_mouse(int32_t dx, int32_t dy, uint8_t buttons);
void gui_handle_scroll(int notches);
void gui_handle_key(char key);
bool gui_needs_redraw(void);
void gui_update_clock(void);
/* Advance time-based app animations at about 20 Hz. */
void gui_tick(void);
void gui_close_window(int idx);
void gui_close_focused(void);
void gui_toggle_start_menu(void);
/* Post a toast notification in the top-right corner of the desktop. */
void gui_notify(const char *title, const char *body);
bool gui_take_logout(void);
void gui_set_bg_color(uint32_t color);
uint32_t gui_get_bg_color(void);
void gui_set_theme(int theme);
int  gui_get_theme(void);
void gui_set_guest_session(bool guest);
bool gui_is_guest_session(void);
int  gui_launch_filebrowser(void);
int  gui_launch_taskmanager(void);
int  gui_launch_pkg(void);
int  gui_launch_editor(void);
int  gui_launch_tetris(void);
int  gui_launch_blocks(void);
int  gui_launch_browser(void);
int  gui_launch_doom(void);
int  gui_launch_ck3(void);
int  gui_launch_app(int slot);
/* Window index for an SPX slot, or -1 when the app is not open. SOLPKG
 * uses this to refuse replacing a running app. */
int  gui_app_window(int slot);

/* Blocking modal save dialog. Opens centered over the desktop, lets the user
 * pick a directory and type a filename. On save returns 1 and fills out_path
 * with a full VFS path ("/home/foo.txt"); returns 0 on cancel. */
int  gui_save_dialog(const char *suggested, char *out_path, int out_max);
int  gui_open_dialog(char *out_path, int out_max);
int  gui_open_with(int slot, const char *path);

#endif

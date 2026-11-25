#include <solis/spx.h>
#include <solis/apps/filebrowser.h>

const struct app_exports __attribute__((section(".spx_exports"), used)) spx_app = {
    .name = "Files",
    .init = filebrowser_init,
    .draw = filebrowser_draw,
    .handle_key = filebrowser_handle_key,
    .handle_mouse = filebrowser_handle_mouse,
    .handle_scroll = filebrowser_handle_scroll,
    .get_size = filebrowser_get_size,
    .handle_context = filebrowser_handle_context,
};

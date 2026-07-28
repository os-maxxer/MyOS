#ifndef NYX_BMP_H
#define NYX_BMP_H

#include <nyx/types.h>

int  bmp_cache_wallpaper(const char *path);
void bmp_blit_wallpaper(void);
int  bmp_is_wallpaper_loaded(void);

#endif

#ifndef SOLIS_BMP_H
#define SOLIS_BMP_H

#include <solis/types.h>

int  bmp_cache_wallpaper(const char *path);
void bmp_blit_wallpaper(void);
int  bmp_is_wallpaper_loaded(void);

#endif

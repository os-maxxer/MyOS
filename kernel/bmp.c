#include <nyx/bmp.h>
#include <nyx/graphics.h>
#include <nyx/vfs.h>
#include <nyx/console.h>

#define BMP_MAX_FILE_SIZE (3 * 1024 * 1024)

#define WALLPAPER_MAX_W 1920
#define WALLPAPER_MAX_H 1200

static uint8_t  bmp_file_buf[BMP_MAX_FILE_SIZE];
static uint32_t wallpaper_cache[WALLPAPER_MAX_W * WALLPAPER_MAX_H];
static uint32_t wallpaper_w = 0;
static uint32_t wallpaper_h = 0;
static int      wallpaper_loaded = 0;

int bmp_cache_wallpaper(const char *path) {
    int fd = vfs_open(path);
    if (fd < 0) {
        return -1;
    }

    int fsize = vfs_get_size(fd);
    if (fsize <= 0 || (uint32_t)fsize > BMP_MAX_FILE_SIZE) {
        return -1;
    }

    int read = vfs_read(fd, bmp_file_buf, (uint32_t)fsize);
    if (read != fsize) {
        return -1;
    }

    const uint8_t *data = bmp_file_buf;
    uint32_t size = (uint32_t)fsize;

    if (size < 54 || data[0] != 'B' || data[1] != 'M') {
        return -1;
    }

    uint32_t data_offset = *(uint32_t*)(data + 10);
    uint32_t dib_size    = *(uint32_t*)(data + 14);
    uint32_t width       = *(uint32_t*)(data + 18);
    int32_t  height      = *(int32_t*)(data + 22);
    uint16_t bpp         = *(uint16_t*)(data + 28);
    uint32_t compression = *(uint32_t*)(data + 30);

    if (dib_size < 40 || compression != 0) {
        return -1;
    }
    if (bpp != 24 && bpp != 32) {
        return -1;
    }
    if (width > WALLPAPER_MAX_W || (uint32_t)(height < 0 ? -height : height) > WALLPAPER_MAX_H) {
        return -1;
    }
    if (data_offset > size) {
        return -1;
    }

    uint32_t abs_h = (height < 0) ? (uint32_t)(-height) : (uint32_t)height;
    int top_down = (height < 0);

    uint32_t src_row_size = ((width * bpp / 8) + 3) & ~3;
    const uint8_t *pixels = data + data_offset;
    uint32_t pixels_avail = size - data_offset;
    uint32_t needed = abs_h * src_row_size;

    if (pixels_avail < needed) {
        return -1;
    }

    for (uint32_t y = 0; y < abs_h; y++) {
        uint32_t src_y = top_down ? y : (abs_h - 1 - y);
        const uint8_t *row = pixels + src_y * src_row_size;
        uint32_t di = 0;

        for (uint32_t x = 0; x < width; x++) {
            if (bpp == 24) {
                wallpaper_cache[y * width + x] = 0xFF000000 |
                    ((uint32_t)row[di + 2] << 16) |
                    ((uint32_t)row[di + 1] << 8) |
                    (uint32_t)row[di];
                di += 3;
            } else {
                wallpaper_cache[y * width + x] =
                    ((uint32_t)row[di + 3] << 24) |
                    ((uint32_t)row[di + 2] << 16) |
                    ((uint32_t)row[di + 1] << 8) |
                    (uint32_t)row[di];
                di += 4;
            }
        }
    }

    wallpaper_w = width;
    wallpaper_h = abs_h;
    wallpaper_loaded = 1;
    return 0;
}

void bmp_blit_wallpaper(void) {
    if (!wallpaper_loaded) return;

    uint32_t fb_w = graphics_get_width();
    uint32_t fb_h = graphics_get_height();

    for (uint32_t y = 0; y < wallpaper_h && y < fb_h; y++) {
        for (uint32_t x = 0; x < wallpaper_w && x < fb_w; x++) {
            graphics_put_pixel(x, y, wallpaper_cache[y * wallpaper_w + x]);
        }
    }
}

int bmp_is_wallpaper_loaded(void) {
    return wallpaper_loaded;
}

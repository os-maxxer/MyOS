#ifndef MYOS_GRAPHICS_H
#define MYOS_GRAPHICS_H

#include <stdbool.h>
#include <myos/types.h>

struct framebuffer_info {
    void *address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t bytes_per_pixel;
    bool present;
};

void graphics_init(uint32_t multiboot_info);
bool graphics_is_ready(void);
void graphics_clear(uint32_t color);
void graphics_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void graphics_draw_string(uint32_t x, uint32_t y, const char *text, uint32_t color);
void graphics_draw_mouse_cursor(uint32_t x, uint32_t y, uint32_t color);

#endif

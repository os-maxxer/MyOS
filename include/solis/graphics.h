#ifndef SOLIS_GRAPHICS_H
#define SOLIS_GRAPHICS_H

#include <stdbool.h>
#include <solis/types.h>

struct framebuffer_info {
    void *address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t bytes_per_pixel;
    bool present;
    uint8_t red_field_position;
    uint8_t green_field_position;
    uint8_t blue_field_position;
};

void graphics_init(uint32_t multiboot_info);
bool graphics_is_ready(void);
void graphics_clear(uint32_t color);
void graphics_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t graphics_get_pixel(uint32_t x, uint32_t y);
void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void graphics_draw_string(uint32_t x, uint32_t y, const char *text, uint32_t color);
void graphics_draw_mouse_cursor(uint32_t x, uint32_t y, uint32_t color);
void graphics_fill_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t color);
void graphics_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t color);
void graphics_fill_gradient_v(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color_top, uint32_t color_bottom);
void graphics_fill_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color);
void graphics_draw_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color);
uint32_t graphics_get_width(void);
uint32_t graphics_get_height(void);
void graphics_blit_rgb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t src_w, const uint32_t *pixels);
void graphics_vbe_init(void);

#endif

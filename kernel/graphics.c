/*
 * Basic framebuffer graphics backend.
 */

#include <myos/graphics.h>
#include <myos/console.h>
#include <myos/multiboot2.h>
#include <stddef.h>

static struct framebuffer_info framebuffer = {0};
static uint32_t *framebuffer_pixels = 0;
static uint32_t framebuffer_width = 0;
static uint32_t framebuffer_height = 0;
static uint32_t framebuffer_pitch = 0;

static uint32_t bg_color = 0xFF1B1B1B;

static void *get_multiboot_tag(uint32_t multiboot_info, uint32_t type) {
    uint32_t *info = (uint32_t *)multiboot_info;
    if (info == 0) {
        return 0;
    }

    uint32_t total_size = info[0];
    uint8_t *ptr = (uint8_t *)(info + 1);
    while ((uint32_t)(ptr - (uint8_t *)info) < total_size) {
        uint32_t tag_type = *(uint32_t *)ptr;
        uint32_t tag_size = *(uint32_t *)(ptr + 4);
        if (tag_type == type) {
            return ptr;
        }
        if (tag_type == 0) {
            break;
        }
        ptr += (tag_size + 7) & ~7U;
    }
    return 0;
}

void graphics_init(uint32_t multiboot_info) {
    struct multiboot_tag_framebuffer *framebuffer_tag = (struct multiboot_tag_framebuffer *)get_multiboot_tag(multiboot_info, 8);
    if (framebuffer_tag == 0 || framebuffer_tag->common.framebuffer_addr == 0) {
        framebuffer.present = false;
        return;
    }

    framebuffer.address = (void *)(uintptr_t)framebuffer_tag->common.framebuffer_addr;
    framebuffer.width = framebuffer_tag->common.framebuffer_width;
    framebuffer.height = framebuffer_tag->common.framebuffer_height;
    framebuffer.pitch = framebuffer_tag->common.framebuffer_pitch;
    framebuffer.bpp = framebuffer_tag->common.framebuffer_bpp;
    framebuffer.bytes_per_pixel = framebuffer.bpp / 8;
    framebuffer.present = true;
    framebuffer_pixels = (uint32_t *)framebuffer.address;
    framebuffer_width = framebuffer.width;
    framebuffer_height = framebuffer.height;
    framebuffer_pitch = framebuffer.pitch / 4;
}

bool graphics_is_ready(void) {
    return framebuffer.present;
}

void graphics_clear(uint32_t color) {
    if (!framebuffer.present) {
        return;
    }
    bg_color = color;
    for (uint32_t y = 0; y < framebuffer_height; ++y) {
        for (uint32_t x = 0; x < framebuffer_width; ++x) {
            framebuffer_pixels[y * framebuffer_pitch + x] = color;
        }
    }
}

void graphics_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer.present || x >= framebuffer_width || y >= framebuffer_height) {
        return;
    }
    framebuffer_pixels[y * framebuffer_pitch + x] = color;
}

void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t row = y; row < y + height && row < framebuffer_height; ++row) {
        for (uint32_t col = x; col < x + width && col < framebuffer_width; ++col) {
            framebuffer_pixels[row * framebuffer_pitch + col] = color;
        }
    }
}

void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    graphics_fill_rect(x, y, width, 1, color);
    graphics_fill_rect(x, y + height - 1, width, 1, color);
    graphics_fill_rect(x, y, 1, height, color);
    graphics_fill_rect(x + width - 1, y, 1, height, color);
}

void graphics_draw_string(uint32_t x, uint32_t y, const char *text, uint32_t color) {
    if (!framebuffer.present) {
        return;
    }
    uint32_t px = x;
    uint32_t py = y;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            py += 16;
            px = x;
            continue;
        }
        if (*p == '\r') {
            px = x;
            continue;
        }
        if (px + 8 >= framebuffer_width || py + 16 >= framebuffer_height) {
            break;
        }
        for (uint32_t row = 0; row < 16; ++row) {
            uint8_t bits = 0;
            if (*p >= ' ' && *p <= '~') {
                bits = 0;
            }
            (void)bits;
            graphics_put_pixel(px, py + row, color);
        }
        px += 8;
    }
}

void graphics_draw_mouse_cursor(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer.present) {
        return;
    }
    for (uint32_t row = 0; row < 16; ++row) {
        for (uint32_t col = 0; col < 16; ++col) {
            graphics_put_pixel(x + col, y + row, color);
        }
    }
}


/*
 * Basic framebuffer graphics backend.
 */

#include <solis/graphics.h>
#include <solis/multiboot2.h>
#include <solis/ports.h>
#include <solis/console.h>
#include <stddef.h>

static struct framebuffer_info framebuffer = {0};
static uint8_t *framebuffer_data = 0;
static uint32_t framebuffer_width = 0;
static uint32_t framebuffer_height = 0;
#define FRAME_STAGE_MAX_W 1280
#define FRAME_STAGE_MAX_H 960
static uint32_t frame_stage[FRAME_STAGE_MAX_W * FRAME_STAGE_MAX_H]
    __attribute__((section(".frame_stage")));
static uint8_t *frame_target;
static uint32_t frame_x, frame_y, frame_w, frame_h;
static uint32_t frame_write_base;
static bool frame_active;

static uint32_t bg_color = 0xFF1B1B1B;

/*
 * Clip rectangle. Every drawing primitive is confined to it, which is what
 * stops an app from painting over its window border, a neighbouring window,
 * or the taskbar. Bounds are half-open: [x0, x1) x [y0, y1).
 */
#define CLIP_STACK_MAX 8
static uint32_t clip_x0, clip_y0, clip_x1, clip_y1;
static struct { uint32_t x0, y0, x1, y1; } clip_stack[CLIP_STACK_MAX];
static int clip_depth = 0;

/* Exact count of pixel writes, so incremental rendering can be measured. */
static volatile uint32_t pixel_writes = 0;

static void clip_init(void) {
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = framebuffer_width;
    clip_y1 = framebuffer_height;
    clip_depth = 0;
}

static void framebuffer_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer.present || x >= framebuffer_width || y >= framebuffer_height) {
        return;
    }
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1) {
        return;
    }
    pixel_writes++;

    uint8_t *pixel = framebuffer_data + y * framebuffer.pitch + x * framebuffer.bytes_per_pixel;
    switch (framebuffer.bytes_per_pixel) {
        case 4: {
            pixel[0] = (uint8_t)(color & 0xFF);
            pixel[1] = (uint8_t)((color >> 8) & 0xFF);
            pixel[2] = (uint8_t)((color >> 16) & 0xFF);
            pixel[3] = (uint8_t)(color >> 24);
            break;
        }
        case 3: {
            uint32_t r = (color >> 16) & 0xFF;
            uint32_t g = (color >> 8) & 0xFF;
            uint32_t b = color & 0xFF;
            uint32_t val = (r << framebuffer.red_field_position) |
                           (g << framebuffer.green_field_position) |
                           (b << framebuffer.blue_field_position);
            pixel[0] = (uint8_t)val;
            pixel[1] = (uint8_t)(val >> 8);
            pixel[2] = (uint8_t)(val >> 16);
            break;
        }
        case 2:
            pixel[0] = (uint8_t)(color & 0xFF);
            pixel[1] = (uint8_t)((color >> 8) & 0xFF);
            break;
        case 1:
            pixel[0] = (uint8_t)(color & 0xFF);
            break;
        default:
            break;
    }
}

static inline void framebuffer_put32(uint32_t x, uint32_t y, uint32_t color) {
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1) {
        return;
    }
    pixel_writes++;
    *(volatile uint32_t *)(framebuffer_data + y * framebuffer.pitch + x * 4) = color;
}

static void *get_multiboot_tag(uint32_t multiboot_info, uint32_t type) {
    uint32_t *info = (uint32_t *)multiboot_info;
    if (info == 0) {
        return 0;
    }

    uint32_t total_size = info[0];
    uint8_t *ptr = (uint8_t *)(info + 2);
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
    framebuffer_data = (uint8_t *)framebuffer.address;
    framebuffer_width = framebuffer.width;
    framebuffer_height = framebuffer.height;

    framebuffer.red_field_position = framebuffer_tag->framebuffer_red_field_position;
    framebuffer.green_field_position = framebuffer_tag->framebuffer_green_field_position;
    framebuffer.blue_field_position = framebuffer_tag->framebuffer_blue_field_position;

    clip_init();

    console_write("[FB] addr=");
    console_write_hex((uint32_t)(uintptr_t)framebuffer.address);
    console_write(" w=");
    console_write_dec(framebuffer.width);
    console_write(" h=");
    console_write_dec(framebuffer.height);
    console_write(" bpp=");
    console_write_dec(framebuffer.bpp);
    console_write(" pitch=");
    console_write_dec(framebuffer.pitch);
    console_write(" bpp4=");
    console_write_dec(framebuffer.bytes_per_pixel);
    console_write("\n");
}

bool graphics_is_ready(void) {
    return framebuffer.present;
}

void graphics_clear(uint32_t color) {
    if (!framebuffer.present) {
        return;
    }
    bg_color = color;
    if (framebuffer.bytes_per_pixel == 4) {
        for (uint32_t y = clip_y0; y < clip_y1; ++y) {
            volatile uint32_t *row =
                (volatile uint32_t *)(framebuffer_data + y * framebuffer.pitch) + clip_x0;
            uint32_t pitch_words = framebuffer.pitch / 4;
            for (uint32_t x = clip_x0; x < clip_x1; ++x) {
                row[x - clip_x0] = color;
            }
            pixel_writes += (clip_x1 - clip_x0);
            row += pitch_words;
        }
    } else {
        for (uint32_t y = clip_y0; y < clip_y1; ++y) {
            for (uint32_t x = clip_x0; x < clip_x1; ++x) {
                framebuffer_set_pixel(x, y, color);
            }
        }
    }
}

void graphics_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    framebuffer_set_pixel(x, y, color);
}
uint32_t graphics_get_pixel(uint32_t x, uint32_t y) {
    if (!framebuffer.present || x >= framebuffer_width || y >= framebuffer_height)
        return 0;
    if (framebuffer.bytes_per_pixel == 4) {
        return *(volatile uint32_t *)(framebuffer_data + y * framebuffer.pitch + x * 4);
    }
    uint8_t *pixel = framebuffer_data + y * framebuffer.pitch + x * framebuffer.bytes_per_pixel;
    switch (framebuffer.bytes_per_pixel) {
        case 4: {
            uint32_t val = *(uint32_t *)pixel;
            uint32_t r = (val >> framebuffer.red_field_position) & 0xFF;
            uint32_t g = (val >> framebuffer.green_field_position) & 0xFF;
            uint32_t b = (val >> framebuffer.blue_field_position) & 0xFF;
            return 0xFF000000 | (r << 16) | (g << 8) | b;
        }
        case 3: {
            uint32_t val = (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) | ((uint32_t)pixel[2] << 16);
            uint32_t r = (val >> framebuffer.red_field_position) & 0xFF;
            uint32_t g = (val >> framebuffer.green_field_position) & 0xFF;
            uint32_t b = (val >> framebuffer.blue_field_position) & 0xFF;
            return 0xFF000000 | (r << 16) | (g << 8) | b;
        }
        case 2: return (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8);
        case 1: return pixel[0];
        default: return 0;
    }
}

/* Intersect a rect with the clip. Done in 64-bit signed arithmetic on
 * purpose: subtracting the clip edge from the rect size underflows when the
 * rect lies entirely outside the clip, and the resulting huge size then
 * slips past the later bounds check and runs the fill loop for billions of
 * iterations. */
static int clip_rect(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    int64_t x0 = *x, y0 = *y;
    int64_t x1 = (int64_t)*x + (int64_t)*w;
    int64_t y1 = (int64_t)*y + (int64_t)*h;

    if (x0 < (int64_t)clip_x0) x0 = clip_x0;
    if (y0 < (int64_t)clip_y0) y0 = clip_y0;
    if (x1 > (int64_t)clip_x1) x1 = clip_x1;
    if (y1 > (int64_t)clip_y1) y1 = clip_y1;

    if (x1 <= x0 || y1 <= y0) return 0;

    *x = (uint32_t)x0;
    *y = (uint32_t)y0;
    *w = (uint32_t)(x1 - x0);
    *h = (uint32_t)(y1 - y0);
    return 1;
}

void graphics_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!framebuffer.present || width == 0 || height == 0) return;
    if (x >= framebuffer_width || y >= framebuffer_height) return;
    if (x + width > framebuffer_width) width = framebuffer_width - x;
    if (y + height > framebuffer_height) height = framebuffer_height - y;

    if (!clip_rect(&x, &y, &width, &height)) return;

    if (framebuffer.bytes_per_pixel == 4) {
        volatile uint32_t *row = (volatile uint32_t *)(framebuffer_data + y * framebuffer.pitch) + x;
        uint32_t pitch_words = framebuffer.pitch / 4;
        for (uint32_t r = 0; r < height; r++) {
            for (uint32_t c = 0; c < width; c++) row[c] = color;
            pixel_writes += width;
            row += pitch_words;
        }
    } else {
        for (uint32_t r = 0; r < height; r++) {
            for (uint32_t c = 0; c < width; c++) {
                framebuffer_set_pixel(x + c, y + r, color);
            }
        }
    }
}

void graphics_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    graphics_fill_rect(x, y, width, 1, color);
    graphics_fill_rect(x, y + height - 1, width, 1, color);
    graphics_fill_rect(x, y, 1, height, color);
    graphics_fill_rect(x + width - 1, y, 1, height, color);
}

static const uint8_t font8x16[128][16] = {
    [' ']  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!']  = {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    ['"']  = {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['#']  = {0x00,0x00,0x24,0x24,0x7E,0x24,0x24,0x24,0x7E,0x24,0x24,0x00,0x00,0x00,0x00,0x00},
    ['$']  = {0x00,0x08,0x3E,0x49,0x48,0x38,0x0E,0x09,0x49,0x3E,0x08,0x00,0x00,0x00,0x00,0x00},
    ['%']  = {0x00,0x00,0x61,0x92,0x64,0x08,0x10,0x26,0x49,0x86,0x00,0x00,0x00,0x00,0x00,0x00},
    ['&']  = {0x00,0x00,0x1C,0x22,0x22,0x14,0x18,0x29,0x46,0x46,0x39,0x00,0x00,0x00,0x00,0x00},
    ['\''] = {0x00,0x18,0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['(']  = {0x00,0x04,0x08,0x10,0x10,0x20,0x20,0x20,0x20,0x10,0x10,0x08,0x04,0x00,0x00,0x00},
    [')']  = {0x00,0x20,0x10,0x08,0x08,0x04,0x04,0x04,0x04,0x08,0x08,0x10,0x20,0x00,0x00,0x00},
    ['*']  = {0x00,0x00,0x08,0x49,0x2A,0x1C,0x2A,0x49,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['+']  = {0x00,0x00,0x00,0x08,0x08,0x08,0x7F,0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00},
    [',']  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x08,0x00,0x00,0x00},
    ['-']  = {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['.']  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    ['/']  = {0x00,0x02,0x04,0x04,0x08,0x08,0x10,0x10,0x20,0x20,0x40,0x00,0x00,0x00,0x00,0x00},
    ['0']  = {0x00,0x00,0x3C,0x46,0x4A,0x52,0x52,0x52,0x52,0x4A,0x46,0x3C,0x00,0x00,0x00,0x00},
    ['1']  = {0x00,0x00,0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00,0x00},
    ['2']  = {0x00,0x00,0x3C,0x42,0x42,0x02,0x04,0x08,0x10,0x20,0x42,0x7E,0x00,0x00,0x00,0x00},
    ['3']  = {0x00,0x00,0x3C,0x42,0x42,0x02,0x1C,0x02,0x02,0x42,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['4']  = {0x00,0x00,0x04,0x0C,0x14,0x24,0x44,0x44,0x7E,0x04,0x04,0x04,0x00,0x00,0x00,0x00},
    ['5']  = {0x00,0x00,0x7E,0x40,0x40,0x40,0x7C,0x02,0x02,0x02,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['6']  = {0x00,0x00,0x1C,0x20,0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['7']  = {0x00,0x00,0x7E,0x42,0x04,0x08,0x08,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00},
    ['8']  = {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['9']  = {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x04,0x38,0x00,0x00,0x00,0x00},
    [':']  = {0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    [';']  = {0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x18,0x18,0x08,0x00,0x00,0x00,0x00},
    ['<']  = {0x00,0x04,0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00},
    ['=']  = {0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['>']  = {0x00,0x40,0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x40,0x00,0x00,0x00,0x00,0x00,0x00},
    ['?']  = {0x00,0x00,0x3C,0x42,0x42,0x02,0x04,0x08,0x08,0x00,0x08,0x08,0x00,0x00,0x00,0x00},
    ['@']  = {0x00,0x00,0x3C,0x42,0x42,0x4E,0x52,0x56,0x4A,0x40,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['A']  = {0x00,0x00,0x18,0x24,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00},
    ['B']  = {0x00,0x00,0x7C,0x22,0x22,0x22,0x3C,0x22,0x22,0x22,0x22,0x7C,0x00,0x00,0x00,0x00},
    ['C']  = {0x00,0x00,0x1C,0x22,0x42,0x40,0x40,0x40,0x40,0x42,0x22,0x1C,0x00,0x00,0x00,0x00},
    ['D']  = {0x00,0x00,0x78,0x24,0x22,0x22,0x22,0x22,0x22,0x22,0x24,0x78,0x00,0x00,0x00,0x00},
    ['E']  = {0x00,0x00,0x7E,0x22,0x20,0x28,0x38,0x28,0x20,0x20,0x22,0x7E,0x00,0x00,0x00,0x00},
    ['F']  = {0x00,0x00,0x7E,0x22,0x20,0x28,0x38,0x28,0x20,0x20,0x20,0x70,0x00,0x00,0x00,0x00},
    ['G']  = {0x00,0x00,0x1C,0x22,0x42,0x40,0x40,0x4E,0x42,0x42,0x22,0x1C,0x00,0x00,0x00,0x00},
    ['H']  = {0x00,0x00,0x42,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00},
    ['I']  = {0x00,0x00,0x3E,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00,0x00},
    ['J']  = {0x00,0x00,0x0F,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x44,0x38,0x00,0x00,0x00,0x00},
    ['K']  = {0x00,0x00,0x62,0x22,0x24,0x28,0x30,0x28,0x24,0x22,0x22,0x62,0x00,0x00,0x00,0x00},
    ['L']  = {0x00,0x00,0x70,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x22,0x7E,0x00,0x00,0x00,0x00},
    ['M']  = {0x00,0x00,0x42,0x66,0x5A,0x5A,0x42,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00},
    ['N']  = {0x00,0x00,0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00},
    ['O']  = {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['P']  = {0x00,0x00,0x7C,0x22,0x22,0x22,0x3C,0x20,0x20,0x20,0x20,0x70,0x00,0x00,0x00,0x00},
    ['Q']  = {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x4A,0x44,0x3A,0x00,0x00,0x00,0x00},
    ['R']  = {0x00,0x00,0x7C,0x22,0x22,0x22,0x3C,0x28,0x24,0x22,0x22,0x62,0x00,0x00,0x00,0x00},
    ['S']  = {0x00,0x00,0x3C,0x42,0x40,0x30,0x0C,0x02,0x02,0x42,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['T']  = {0x00,0x00,0x7F,0x49,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x1C,0x00,0x00,0x00,0x00},
    ['U']  = {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['V']  = {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x24,0x18,0x00,0x00,0x00,0x00,0x00},
    ['W']  = {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00,0x00,0x00,0x00,0x00},
    ['X']  = {0x00,0x00,0x42,0x42,0x24,0x18,0x18,0x24,0x42,0x42,0x42,0x00,0x00,0x00,0x00,0x00},
    ['Y']  = {0x00,0x00,0x41,0x22,0x14,0x08,0x08,0x08,0x08,0x08,0x08,0x1C,0x00,0x00,0x00,0x00},
    ['Z']  = {0x00,0x00,0x7E,0x42,0x04,0x08,0x10,0x10,0x20,0x42,0x42,0x7E,0x00,0x00,0x00,0x00},
    ['[']  = {0x00,0x1E,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x1E,0x00,0x00,0x00},
    ['\\'] = {0x00,0x40,0x20,0x20,0x10,0x10,0x08,0x08,0x04,0x04,0x02,0x00,0x00,0x00,0x00,0x00},
    [']']  = {0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x78,0x00,0x00,0x00},
    ['^']  = {0x00,0x08,0x14,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['_']  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x00,0x00},
    ['`']  = {0x00,0x10,0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['a']  = {0x00,0x00,0x00,0x00,0x3C,0x42,0x02,0x3E,0x42,0x42,0x46,0x3A,0x00,0x00,0x00,0x00},
    ['b']  = {0x00,0x00,0x60,0x20,0x20,0x3C,0x22,0x22,0x22,0x22,0x22,0x3C,0x00,0x00,0x00,0x00},
    ['c']  = {0x00,0x00,0x00,0x00,0x3C,0x42,0x40,0x40,0x40,0x42,0x22,0x1C,0x00,0x00,0x00,0x00},
    ['d']  = {0x00,0x00,0x0C,0x04,0x04,0x3C,0x44,0x44,0x44,0x44,0x44,0x3C,0x00,0x00,0x00,0x00},
    ['e']  = {0x00,0x00,0x00,0x00,0x3C,0x42,0x42,0x7E,0x40,0x40,0x22,0x1C,0x00,0x00,0x00,0x00},
    ['f']  = {0x00,0x00,0x0C,0x12,0x10,0x7C,0x10,0x10,0x10,0x10,0x10,0x7C,0x00,0x00,0x00,0x00},
    ['g']  = {0x00,0x00,0x00,0x00,0x3C,0x44,0x44,0x44,0x3C,0x04,0x44,0x38,0x00,0x00,0x00,0x00},
    ['h']  = {0x00,0x00,0x60,0x20,0x20,0x2C,0x32,0x22,0x22,0x22,0x22,0x66,0x00,0x00,0x00,0x00},
    ['i']  = {0x00,0x00,0x08,0x08,0x00,0x18,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00,0x00},
    ['j']  = {0x00,0x00,0x04,0x04,0x00,0x0C,0x04,0x04,0x04,0x04,0x04,0x44,0x44,0x38,0x00,0x00},
    ['k']  = {0x00,0x00,0x60,0x20,0x20,0x24,0x28,0x30,0x28,0x24,0x22,0x66,0x00,0x00,0x00,0x00},
    ['l']  = {0x00,0x00,0x18,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00,0x00},
    ['m']  = {0x00,0x00,0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x49,0x49,0x00,0x00,0x00,0x00,0x00},
    ['n']  = {0x00,0x00,0x00,0x00,0x5C,0x22,0x22,0x22,0x22,0x22,0x22,0x00,0x00,0x00,0x00,0x00},
    ['o']  = {0x00,0x00,0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00},
    ['p']  = {0x00,0x00,0x00,0x00,0x7C,0x22,0x22,0x22,0x3C,0x20,0x20,0x70,0x00,0x00,0x00,0x00},
    ['q']  = {0x00,0x00,0x00,0x00,0x3E,0x44,0x44,0x44,0x3C,0x04,0x04,0x0E,0x00,0x00,0x00,0x00},
    ['r']  = {0x00,0x00,0x00,0x00,0x6C,0x32,0x22,0x20,0x20,0x20,0x70,0x00,0x00,0x00,0x00,0x00},
    ['s']  = {0x00,0x00,0x00,0x00,0x3C,0x42,0x30,0x0C,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,0x00},
    ['t']  = {0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x10,0x10,0x12,0x0C,0x00,0x00,0x00,0x00,0x00},
    ['u']  = {0x00,0x00,0x00,0x00,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x3C,0x00,0x00,0x00,0x00},
    ['v']  = {0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x24,0x18,0x00,0x00,0x00,0x00,0x00},
    ['w']  = {0x00,0x00,0x00,0x00,0x41,0x49,0x49,0x49,0x49,0x49,0x36,0x00,0x00,0x00,0x00,0x00},
    ['x']  = {0x00,0x00,0x00,0x00,0x66,0x24,0x18,0x18,0x24,0x42,0x42,0x00,0x00,0x00,0x00,0x00},
    ['y']  = {0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x46,0x3A,0x02,0x42,0x3C,0x00,0x00},
    ['z']  = {0x00,0x00,0x00,0x00,0x7E,0x44,0x08,0x10,0x20,0x42,0x7E,0x00,0x00,0x00,0x00,0x00},
    ['{']  = {0x00,0x06,0x08,0x08,0x08,0x08,0x30,0x08,0x08,0x08,0x08,0x08,0x06,0x00,0x00,0x00},
    ['|']  = {0x00,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
    ['}']  = {0x00,0x60,0x10,0x10,0x10,0x10,0x0C,0x10,0x10,0x10,0x10,0x10,0x60,0x00,0x00,0x00},
    ['~']  = {0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x49,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

void graphics_draw_string(uint32_t x, uint32_t y, const char *text, uint32_t color) {
    if (!framebuffer.present) {
        return;
    }
    uint32_t px = x;
    uint32_t py = y;
    bool fast = (framebuffer.bytes_per_pixel == 4);
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
        /* Stop once the glyph starts past the clip edge. Descenders are not
         * an issue because the 16px cell fully contains every glyph. */
        if (px >= clip_x1 || py >= clip_y1) {
            break;
        }
        if (px + 8 <= clip_x0 || py + 16 <= clip_y0) {
            px += 8;
            continue;
        }
        unsigned char ch = (unsigned char)*p;
        if (ch > 127) ch = '?';
        for (uint32_t row = 0; row < 16; ++row) {
            uint8_t bits = font8x16[ch][row];
            if (!bits) continue;
            for (uint32_t col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    if (fast) framebuffer_put32(px + col, py + row, color);
                    else framebuffer_set_pixel(px + col, py + row, color);
                }
            }
        }
        px += 8;
    }
}

uint32_t graphics_get_width(void) {
    return framebuffer_width;
}

uint32_t graphics_get_height(void) {
    return framebuffer_height;
}

void graphics_reset_clip(void) {
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = framebuffer_width;
    clip_y1 = framebuffer_height;
    clip_depth = 0;
}

void graphics_set_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    clip_x0 = x;
    clip_y0 = y;
    clip_x1 = x + w;
    clip_y1 = y + h;
    if (clip_x1 > framebuffer_width) clip_x1 = framebuffer_width;
    if (clip_y1 > framebuffer_height) clip_y1 = framebuffer_height;
    if (clip_x0 > clip_x1) clip_x0 = clip_x1;
    if (clip_y0 > clip_y1) clip_y0 = clip_y1;
}

void graphics_get_clip(uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h) {
    if (x) *x = clip_x0;
    if (y) *y = clip_y0;
    if (w) *w = clip_x1 - clip_x0;
    if (h) *h = clip_y1 - clip_y0;
}

/* Intersect the new region with the current clip and remember the old one,
 * so nested layers (window inside desktop inside damage rect) compose. */
void graphics_push_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (clip_depth < CLIP_STACK_MAX) {
        clip_stack[clip_depth].x0 = clip_x0;
        clip_stack[clip_depth].y0 = clip_y0;
        clip_stack[clip_depth].x1 = clip_x1;
        clip_stack[clip_depth].y1 = clip_y1;
        clip_depth++;
    }
    /* Intersect against the saved rectangle rather than shrinking w/h in
     * place, which would underflow for a region entirely outside the clip. */
    uint32_t nx = clip_x0, ny = clip_y0;
    uint32_t nw = clip_x1 - clip_x0, nh = clip_y1 - clip_y0;
    int64_t x0 = x, y0 = y, x1 = (int64_t)x + w, y1 = (int64_t)y + h;
    if (x0 < nx) x0 = nx;
    if (y0 < ny) y0 = ny;
    if (x1 > (int64_t)nx + nw) x1 = (int64_t)nx + nw;
    if (y1 > (int64_t)ny + nh) y1 = (int64_t)ny + nh;
    if (x1 <= x0 || y1 <= y0) { w = 0; h = 0; x = nx; y = ny; }
    else { x = (uint32_t)x0; y = (uint32_t)y0; w = (uint32_t)(x1 - x0); h = (uint32_t)(y1 - y0); }
    graphics_set_clip(x, y, w, h);
}

void graphics_pop_clip(void) {
    if (clip_depth <= 0) return;
    clip_depth--;
    clip_x0 = clip_stack[clip_depth].x0;
    clip_y0 = clip_stack[clip_depth].y0;
    clip_x1 = clip_stack[clip_depth].x1;
    clip_y1 = clip_stack[clip_depth].y1;
}

bool graphics_begin_frame(void) {
    if (!framebuffer.present || frame_active || framebuffer.bytes_per_pixel != 4 ||
        framebuffer_width > FRAME_STAGE_MAX_W || framebuffer_height > FRAME_STAGE_MAX_H ||
        framebuffer.pitch > FRAME_STAGE_MAX_W * 4)
        return false;

    graphics_get_clip(&frame_x, &frame_y, &frame_w, &frame_h);
    frame_target = framebuffer_data;
    frame_write_base = pixel_writes;
    for (uint32_t y = frame_y; y < frame_y + frame_h; y++) {
        volatile uint32_t *src = (volatile uint32_t *)(frame_target + y * framebuffer.pitch) + frame_x;
        uint32_t *dst = (uint32_t *)((uint8_t *)frame_stage + y * framebuffer.pitch) + frame_x;
        for (uint32_t x = 0; x < frame_w; x++) dst[x] = src[x];
    }
    framebuffer_data = (uint8_t *)frame_stage;
    frame_active = true;
    return true;
}

void graphics_end_frame(void) {
    if (!frame_active) return;
    uint8_t *stage = framebuffer_data;
    framebuffer_data = frame_target;
    for (uint32_t y = frame_y; y < frame_y + frame_h; y++) {
        volatile uint32_t *dst = (volatile uint32_t *)(frame_target + y * framebuffer.pitch) + frame_x;
        uint32_t *src = (uint32_t *)(stage + y * framebuffer.pitch) + frame_x;
        for (uint32_t x = 0; x < frame_w; x++) dst[x] = src[x];
    }
    pixel_writes = frame_write_base + frame_w * frame_h;
    frame_active = false;
}

uint32_t graphics_pixel_writes(void) {
    return pixel_writes;
}

void graphics_reset_pixel_counter(void) {
    pixel_writes = 0;
}

void graphics_blit_rgb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t src_w, const uint32_t *pixels) {
    if (!framebuffer.present || w == 0 || h == 0 || pixels == 0) return;
    if (x >= framebuffer_width || y >= framebuffer_height) return;
    if (x + w > framebuffer_width) w = framebuffer_width - x;
    if (y + h > framebuffer_height) h = framebuffer_height - y;
    if (!clip_rect(&x, &y, &w, &h)) return;

    if (framebuffer.bytes_per_pixel == 4) {
        volatile uint32_t *row = (volatile uint32_t *)(framebuffer_data + y * framebuffer.pitch) + x;
        uint32_t pitch_words = framebuffer.pitch / 4;
        for (uint32_t r = 0; r < h; r++) {
            const uint32_t *src = pixels + (uint32_t)r * src_w;
            for (uint32_t c = 0; c < w; c++) row[c] = src[c];
            pixel_writes += w;
            row += pitch_words;
        }
    } else {
        for (uint32_t r = 0; r < h; r++) {
            for (uint32_t c = 0; c < w; c++) {
                graphics_put_pixel(x + c, y + r, pixels[r * src_w + c]);
            }
        }
    }
}

void graphics_draw_mouse_cursor(uint32_t x, uint32_t y, uint32_t color) {
    if (!framebuffer.present) return;
    static const uint16_t arrow[16] = {
        0x8000, 0xC000, 0xA000, 0x9000,
        0x8800, 0x8400, 0x8200, 0x8100,
        0x8000, 0x8000, 0x9C00, 0xA200,
        0xC100, 0x8000, 0x0000, 0x0000
    };
    uint32_t black = 0xFF000000;
    for (uint32_t row = 0; row < 16; row++) {
        uint16_t bits = arrow[row];
        if (!bits) continue;
        for (uint32_t col = 0; col < 16; col++) {
            if (!(bits & (0x8000 >> col))) continue;
            if (col > 0) graphics_put_pixel(x + col - 1, y + row, black);
            graphics_put_pixel(x + col + 1, y + row, black);
            if (row > 0) graphics_put_pixel(x + col, y + row - 1, black);
            graphics_put_pixel(x + col, y + row + 1, black);
        }
    }
    for (uint32_t row = 0; row < 16; row++) {
        uint16_t bits = arrow[row];
        if (!bits) continue;
        for (uint32_t col = 0; col < 16; col++) {
            if (bits & (0x8000 >> col))
                graphics_put_pixel(x + col, y + row, color);
        }
    }
}

static uint32_t lerp_color_gr(uint32_t c1, uint32_t c2, int t, int max) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + ((r2 - r1) * t / max);
    uint8_t g = g1 + ((g2 - g1) * t / max);
    uint8_t b = b1 + ((b2 - b1) * t / max);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void graphics_fill_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t color) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r == 0) { graphics_fill_rect(x, y, w, h, color); return; }
    graphics_fill_rect(x + r, y, w - r * 2, h, color);
    graphics_fill_rect(x, y + r, r, h - r * 2, color);
    graphics_fill_rect(x + w - r, y + r, r, h - r * 2, color);
    for (uint32_t cy = 0; cy <= r; cy++) {
        for (uint32_t cx = 0; cx <= r; cx++) {
            if (cx * cx + cy * cy <= r * r) {
                graphics_put_pixel(x + r - cx, y + r - cy, color);
                graphics_put_pixel(x + w - r - 1 + cx, y + r - cy, color);
                graphics_put_pixel(x + r - cx, y + h - r - 1 + cy, color);
                graphics_put_pixel(x + w - r - 1 + cx, y + h - r - 1 + cy, color);
            }
        }
    }
}

void graphics_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t color) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r == 0) { graphics_draw_rect(x, y, w, h, color); return; }
    graphics_fill_rect(x + r, y, w - r * 2, 1, color);
    graphics_fill_rect(x + r, y + h - 1, w - r * 2, 1, color);
    graphics_fill_rect(x, y + r, 1, h - r * 2, color);
    graphics_fill_rect(x + w - 1, y + r, 1, h - r * 2, color);
    for (uint32_t cy = 0; cy <= r; cy++) {
        for (uint32_t cx = 0; cx <= r; cx++) {
            if (cx == 0 || cy == 0) continue;
            if (cx * cx + cy * cy <= r * r && (cx+1)*(cx+1) + (cy+1)*(cy+1) > r*r) {
                graphics_put_pixel(x + r - cx, y + r - cy, color);
                graphics_put_pixel(x + w - r - 1 + cx, y + r - cy, color);
                graphics_put_pixel(x + r - cx, y + h - r - 1 + cy, color);
                graphics_put_pixel(x + w - r - 1 + cx, y + h - r - 1 + cy, color);
            }
        }
    }
}

void graphics_fill_gradient_v(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color_top, uint32_t color_bottom) {
    for (uint32_t row = 0; row < h; row++) {
        graphics_fill_rect(x, y + row, w, 1, lerp_color_gr(color_top, color_bottom, row, h));
    }
}

void graphics_fill_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color) {
    if (!framebuffer.present) return;
    if (framebuffer.bytes_per_pixel == 4) {
        for (int32_t dy = -(int32_t)r; dy <= (int32_t)r; dy++) {
            int32_t ady = dy < 0 ? -dy : dy;
            int32_t adx = 0;
            while ((adx + 1) * (adx + 1) + ady * ady <= (int32_t)(r * r)) adx++;
            /* Clamp the span to the clip: this path writes straight to the
             * framebuffer, so without this a circle would paint outside the
             * window it belongs to. */
            int64_t sx = (int64_t)cx - adx;
            int64_t ex = (int64_t)cx + adx;
            if (sx < (int64_t)clip_x0) sx = clip_x0;
            if (ex > (int64_t)clip_x1 - 1) ex = (int64_t)clip_x1 - 1;
            int64_t sy = (int64_t)cy + dy;
            if (sy < (int64_t)clip_y0 || sy >= (int64_t)clip_y1) continue;
            if (sx < 0) sx = 0;
            if (ex >= (int64_t)framebuffer_width) ex = (int64_t)framebuffer_width - 1;
            if (ex < sx) continue;
            volatile uint32_t *row =
                (volatile uint32_t *)(framebuffer_data + (uint32_t)sy * framebuffer.pitch) + (uint32_t)sx;
            for (int64_t c = sx; c <= ex; c++) row[c - sx] = color;
            pixel_writes += (uint32_t)(ex - sx + 1);
        }
        return;
    }
    for (uint32_t dy = 0; dy <= r; dy++) {
        for (uint32_t dx = 0; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                graphics_put_pixel(cx + dx, cy + dy, color);
                if (dx) graphics_put_pixel(cx - dx, cy + dy, color);
                if (dy) graphics_put_pixel(cx + dx, cy - dy, color);
                if (dx && dy) graphics_put_pixel(cx - dx, cy - dy, color);
            }
        }
    }
}

void graphics_draw_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color) {
    for (int32_t dy = -(int32_t)r; dy <= (int32_t)r; dy++) {
        for (int32_t dx = -(int32_t)r; dx <= (int32_t)r; dx++) {
            int32_t d = dx * dx + dy * dy;
            int32_t rr = (int32_t)r;
            if (d >= rr * rr && d < (rr + 1) * (rr + 1)) {
                graphics_put_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

#define VBE_DISPI_INDEX_ID              0
#define VBE_DISPI_INDEX_XRES            1
#define VBE_DISPI_INDEX_YRES            2
#define VBE_DISPI_INDEX_BPP             3
#define VBE_DISPI_INDEX_ENABLE          4

#define VBE_DISPI_DISABLED              0x00
#define VBE_DISPI_ENABLED               0x01
#define VBE_DISPI_LFB_ENABLED           0x40
#define VBE_DISPI_NOCLEARMEM            0x80

void graphics_vbe_init(void) {
    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    outw(VBE_DISPI_IOPORT_DATA, 0xB0C5);
    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    uint16_t id = inw(VBE_DISPI_IOPORT_DATA);
    if (id != 0xB0C5) {
        return;
    }

    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    outw(VBE_DISPI_IOPORT_DATA, VBE_DISPI_DISABLED);

    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
    outw(VBE_DISPI_IOPORT_DATA, (uint16_t)framebuffer_width);
    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
    outw(VBE_DISPI_IOPORT_DATA, (uint16_t)framebuffer_height);
    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
    outw(VBE_DISPI_IOPORT_DATA, (uint16_t)framebuffer.bpp);

    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    outw(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
}

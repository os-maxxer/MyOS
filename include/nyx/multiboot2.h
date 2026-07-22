#ifndef NYX_MULTIBOOT2_H
#define NYX_MULTIBOOT2_H

#include <nyx/types.h>

struct multiboot_tag_framebuffer_common {
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
};

struct multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    struct multiboot_tag_framebuffer_common common;
};

#endif

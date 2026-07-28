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

#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO 4

struct multiboot_tag_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_tag_mmap_entry entries[];
};

struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

#endif

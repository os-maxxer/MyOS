#ifndef SOLIS_SPX_H
#define SOLIS_SPX_H

#include <solis/types.h>

#define SPX_MAGIC    0x5350580A
#define SPX_VERSION  3
#define SPX_MAX_APPS 14
#define SPX_NAME_LEN 12

struct spx_header {
    uint32_t magic;
    uint32_t version;
    uint32_t code_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t exports_offset;
    char     name[SPX_NAME_LEN];
    /* Version 3: the slot base the image was linked for, or 0 for a package
     * that can load into any free slot. A non-zero value pins the package to
     * exactly one slot, which is what makes runtime installs safe: the code
     * contains absolute addresses resolved against that base. */
    uint32_t load_base;
} __attribute__((packed));

/* Optional integrity footer appended after the code image by mkspx.py. It is
 * *outside* the header on purpose: changing sizeof(struct spx_header) would
 * relocate the code for every already-built package. A loader that does not
 * know about the footer ignores the trailing bytes, and a package without one
 * is still accepted, so the format stays backward compatible. */
#define SPX_FOOTER_MAGIC 0x46585053u

struct spx_footer {
    uint32_t magic;          /* SPX_FOOTER_MAGIC */
    uint32_t crc;            /* CRC-32 of the header plus the code image */
    char     version[16];    /* package version, e.g. "1.4.0" */
    char     stamp[24];      /* build stamp that produced the package */
} __attribute__((packed));

uint32_t spx_crc32(const uint8_t *data, uint32_t size);

struct embedded_spx {
    const char *name;
    const uint8_t *data;
    uint32_t size;
};

struct app_exports {
    const char *name;
    void (*init)(void);
    void (*draw)(int x, int y, int w, int h);
    void (*handle_key)(char key);
    void (*handle_mouse)(int x, int y, int w, int h, int mx, int my);
    void (*get_size)(int *w, int *h);
    /* Optional: vertical scroll wheel over the app's client area. `notches`
     * is positive when scrolling up. Called only when the pointer is inside
     * the client rect. */
    void (*handle_scroll)(int x, int y, int w, int h, int notches);
    void (*handle_context)(int x, int y, int w, int h, int mx, int my);
    void (*open_file)(const char *path);
    /* Optional: advance time-based state. Called about 20 times a second
     * while the window is visible; returning non-zero asks the desktop to
     * repaint the client area. Keep draw() free of state changes so partial
     * repaints (cursor, damage) cannot corrupt the frame. */
    int (*tick)(void);
};

#define SPX_SLOT_BASE   0x01000000
/* Each built-in app gets 3 MiB of code/BSS space. Bases stay 3 MiB apart
 * so fourteen slots end at 0x03A00000, where the kernel's user-image
 * staging buffer starts. That buffer is mapped read/write but never
 * executable, so app code must not be loaded into it. */
#define SPX_SLOT_SIZE   0x00300000
#define SPX_SLOT_BASE_FOR(slot) (SPX_SLOT_BASE + SPX_SLOT_SIZE * (uint32_t)(slot))

/* Negative spx_install() results. */
#define SPX_ERR_BAD    (-1) /* malformed or truncated image */
#define SPX_ERR_BUSY   (-2) /* the image's slot holds a different app */
#define SPX_ERR_BASE   (-3) /* load_base is not a valid slot base */
#define SPX_ERR_CRC    (-4) /* integrity footer does not match the image */

/* Slot that owns a given linked base, or -1 when the base is not one of the
 * SPX slots. */
int  spx_slot_for_base(uint32_t base);

void spx_init(struct embedded_spx *apps, int count);
const struct app_exports *spx_get_exports(int slot);
int  spx_install(const uint8_t *data, uint32_t size);
int  spx_uninstall(const char *name);
int  spx_find_slot(const char *name);
int  spx_list_installed(char names[][SPX_NAME_LEN], int max);

void sys_init_ram(uintptr_t multiboot_info);
void sys_init_cpu(void);
uint32_t sys_get_total_ram(void);
void sys_get_cpu_brand(char *buf, int max_len);
uint64_t sys_get_machine_id(void);

enum spx_app_id {
    SPX_TERMINAL    = 0,
    SPX_NOTEPAD     = 1,
    SPX_PAINT       = 2,
    SPX_SETTINGS    = 3,
    SPX_FILEBROWSER = 4,
    SPX_TASKMANAGER = 5,
    SPX_PKG         = 6,
    SPX_EDITOR      = 7,
    SPX_TETRIS      = 8,
    SPX_BLOCKS      = 9,
    SPX_CALCULATOR  = 10,
    SPX_DOOM        = 11,
    SPX_BROWSER     = 12,
    SPX_CK3         = 13,
};

#endif

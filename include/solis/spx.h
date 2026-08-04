#ifndef SOLIS_SPX_H
#define SOLIS_SPX_H

#include <solis/types.h>

#define SPX_MAGIC    0x5350580A
#define SPX_VERSION  1
#define SPX_MAX_APPS 10
#define SPX_NAME_LEN 12

struct spx_header {
    uint32_t magic;
    uint32_t version;
    uint32_t code_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t exports_offset;
    char     name[SPX_NAME_LEN];
} __attribute__((packed));

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
};

#define SPX_SLOT_BASE   0x01000000
#define SPX_SLOT_SIZE   0x00200000

void spx_init(struct embedded_spx *apps, int count);
const struct app_exports *spx_get_exports(int slot);
int  spx_install(const uint8_t *data, uint32_t size);
int  spx_uninstall(const char *name);
int  spx_find_slot(const char *name);
int  spx_list_installed(char names[][SPX_NAME_LEN], int max);

void sys_init_ram(uint32_t multiboot_info);
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
};

#endif

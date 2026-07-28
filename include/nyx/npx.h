#ifndef NYX_NPX_H
#define NYX_NPX_H

#include <nyx/types.h>

#define NPX_MAGIC    0x4E50580A
#define NPX_VERSION  1
#define NPX_MAX_APPS 8
#define NPX_NAME_LEN 12

struct npx_header {
    uint32_t magic;
    uint32_t version;
    uint32_t code_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t exports_offset;
    char     name[NPX_NAME_LEN];
} __attribute__((packed));

struct embedded_npx {
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

#define NPX_SLOT_BASE   0x01000000
#define NPX_SLOT_SIZE   0x00200000

void npx_init(struct embedded_npx *apps, int count);
const struct app_exports *npx_get_exports(int slot);
int  npx_install(const uint8_t *data, uint32_t size);
int  npx_uninstall(const char *name);
int  npx_find_slot(const char *name);
int  npx_list_installed(char names[][NPX_NAME_LEN], int max);

void sys_init_ram(uint32_t multiboot_info);
void sys_init_cpu(void);
uint32_t sys_get_total_ram(void);
void sys_get_cpu_brand(char *buf, int max_len);
uint64_t sys_get_machine_id(void);

enum npx_app_id {
    NPX_TERMINAL    = 0,
    NPX_NOTEPAD     = 1,
    NPX_PAINT       = 2,
    NPX_SETTINGS    = 3,
    NPX_FILEBROWSER = 4,
    NPX_TASKMANAGER = 5,
    NPX_PKG         = 6,
};

#endif

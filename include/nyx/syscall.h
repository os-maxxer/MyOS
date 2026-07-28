#ifndef NYX_SYSCALL_H
#define NYX_SYSCALL_H

#include <nyx/types.h>

#define SYSCALL_TABLE_ADDR 0x00007E00

struct syscall_table {
    void (*exit)(int code);
    void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*draw_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*draw_string)(uint32_t x, uint32_t y, const char *text, uint32_t color);
    void (*put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    uint32_t (*get_pixel)(uint32_t x, uint32_t y);
    uint32_t (*get_width)(void);
    uint32_t (*get_height)(void);
    uint32_t (*get_ticks)(void);
    void (*outb)(uint16_t port, uint8_t value);

    const char* (*vfs_get_cwd)(void);
    int  (*vfs_cd)(const char *path);
    int  (*vfs_mkdir)(const char *path);
    int  (*vfs_ls)(char names[][24], int max);
    int  (*vfs_ls_at)(const char *path, char names[][24], int max);
    int  (*vfs_open)(const char *path);
    int  (*vfs_read)(int fd, uint8_t *buf, uint32_t size);
    int  (*vfs_write)(int fd, const uint8_t *buf, uint32_t size);
    int  (*vfs_create)(const char *path);
    int  (*vfs_delete)(const char *path);
    int  (*vfs_get_size)(int fd);

    int  (*nofs_list)(char names[][24], int max);
    int  (*nofs_create)(const char *name);
    int  (*nofs_open)(const char *name);
    int  (*nofs_read)(int fd, uint8_t *buf, uint32_t size);
    int  (*nofs_write)(int fd, const uint8_t *buf, uint32_t size);
    int  (*nofs_get_size)(int fd);
    uint64_t (*nofs_get_machine_id)(void);

    int  (*gui_get_theme)(void);
    void (*gui_set_bg_color)(uint32_t color);
    void (*gui_set_theme)(int theme);

    int  (*ata_present)(void);
    const char* (*ata_get_serial)(void);

    /* System info */
    uint32_t (*sys_get_total_ram)(void);
    void (*sys_get_cpu_brand)(char *buf, int max_len);
    int  (*npx_install)(const uint8_t *data, uint32_t size);
    int  (*npx_uninstall)(const char *name);
    int  (*npx_find_slot)(const char *name);
    int  (*npx_list_installed)(char names[][12], int max);
    uint64_t (*sys_get_machine_id)(void);
    int  (*net_available)(void);
    int  (*net_http_get)(const uint8_t *ip, uint16_t port,
                         const char *host, const char *path,
                         uint8_t *response, uint32_t max_size);
    int  (*gui_launch_app)(int slot_id);
    int  (*net_ping)(const uint8_t *ip, uint32_t timeout_ms);
    int  (*net_arp_resolve)(const uint8_t *ip, uint8_t *mac);
    int  (*net_dns_resolve)(const char *hostname, uint8_t *ip_out);
    int  (*dbg_read)(char *buf, int max);
};

#endif

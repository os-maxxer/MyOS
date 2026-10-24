#ifndef SOLIS_SYSCALL_H
#define SOLIS_SYSCALL_H

#include <solis/types.h>
#include <solis/rtc.h>
#include <solis/dbg.h>

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

    int  (*solfs_list)(char names[][24], int max);
    int  (*solfs_create)(const char *name);
    int  (*solfs_open)(const char *name);
    int  (*solfs_read)(int fd, uint8_t *buf, uint32_t size);
    int  (*solfs_write)(int fd, const uint8_t *buf, uint32_t size);
    int  (*solfs_get_size)(int fd);
    uint64_t (*solfs_get_machine_id)(void);

    int  (*gui_get_theme)(void);
    void (*gui_set_bg_color)(uint32_t color);
    void (*gui_set_theme)(int theme);

    int  (*ata_present)(void);
    const char* (*ata_get_serial)(void);
    const char* (*ata_get_model)(void);

    /* System info */
    uint32_t (*sys_get_total_ram)(void);
    void (*sys_get_cpu_brand)(char *buf, int max_len);
    int  (*spx_install)(const uint8_t *data, uint32_t size);
    int  (*spx_uninstall)(const char *name);
    int  (*spx_find_slot)(const char *name);
    int  (*spx_list_installed)(char names[][12], int max);
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
    void (*dbg_get_health)(struct dbg_health *out);
    void (*fill_circle)(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color);

    void (*rtc_get_time)(struct rtc_time *out);
    void (*rtc_set_timezone)(int index);
    int  (*rtc_get_timezone)(void);
    int  (*rtc_get_timezone_count)(void);
    void (*rtc_get_timezone_name)(int index, char *buf, int max_len);
    int  (*rtc_get_timezone_offset)(int index);

    /* Modal save dialog. Returns 1 on save with out_path filled, 0 on cancel. */
    int  (*gui_save_dialog)(const char *suggested, char *out_path, int out_max);
    int  (*gui_open_dialog)(char *out_path, int out_max);
    int  (*gui_open_with)(int slot, const char *path);
};

#endif

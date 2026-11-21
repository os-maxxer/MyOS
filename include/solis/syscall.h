#ifndef SOLIS_SYSCALL_H
#define SOLIS_SYSCALL_H

#include <solis/types.h>
#include <solis/rtc.h>
#include <solis/dbg.h>
#include <solis/net.h>
#include <stdbool.h>

#define SYSCALL_TABLE_ADDR 0x00007E00

struct app_exports;

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
    int  (*setup_set_theme)(int theme);

    int  (*ata_present)(void);
    const char* (*ata_get_serial)(void);
    const char* (*ata_get_model)(void);

    /* System info */
    uint32_t (*sys_get_total_ram)(void);
    void (*sys_get_cpu_brand)(char *buf, int max_len);
    int  (*jit_execute)(const uint8_t *code, uint32_t code_size,
                        const uint8_t *data, uint32_t data_size);
    int  (*spx_install)(const uint8_t *data, uint32_t size);
    int  (*spx_uninstall)(const char *name);
    int  (*spx_find_slot)(const char *name);
    int  (*spx_list_installed)(char names[][12], int max);
    uint64_t (*sys_get_machine_id)(void);
    int  (*net_available)(void);
    const char *(*net_get_device_name)(void);
    int  (*net_wifi_adapter_present)(void);
    void (*net_get_ip)(uint8_t *ip);
    void (*net_get_gateway)(uint8_t *ip);
    void (*net_get_dns)(uint8_t *ip);
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

    /* SolFS capacity and usage, in bytes. Zero when no disk is mounted. */
    uint64_t (*solfs_get_total_bytes)(void);
    uint64_t (*solfs_get_free_bytes)(void);
    uint64_t (*solfs_get_used_bytes)(void);

    bool (*setup_is_installed)(void);
    int  (*setup_change_password)(const char *password);
    int  (*setup_edition)(void);
    bool (*setup_is_guest)(void);

    /* Fast path for software-rendered apps (Doom). The kernel clips the
     * rectangle and scales each source row into the destination. */
    void (*blit_rgb_scaled)(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            const uint32_t *src, uint32_t src_w,
                            uint32_t src_h);

    /* Read-only shareware WAD embedded in the kernel image. */
    const uint8_t *doom_wad;
    uint32_t doom_wad_size;

    /* Restart-safe edition changes. setup_pending_edition() returns the
     * current edition when no change is queued. */
    int (*setup_pending_edition)(void);
    int (*setup_request_edition)(int edition);

    int (*net_https_get)(const uint8_t *ip, uint16_t port,
                         const char *host, const char *path,
                         uint8_t *response, uint32_t max_size);

    int (*audio_available)(void);
    int (*audio_play_tone)(uint32_t hz, uint32_t ms);

    /* UTF-8 aware text measurement, matching graphics_draw_string. */
    int (*text_width)(const char *text);

    /* Package slot introspection for SOLPKG: the exports of a loaded app,
     * and the slot a linked base maps to. */
    const struct app_exports *(*spx_get_exports)(int slot);
    int (*spx_slot_for_base)(uint32_t base);
    int (*gui_app_window)(int slot);

    /* Incremental download so SOLPKG can draw a progress bar. */
    int  (*net_dl_begin)(const char *url, uint8_t *buf, uint32_t max_size);
    int  (*net_dl_step)(struct net_dl_progress *progress);
    void (*net_dl_abort)(void);
};

#endif

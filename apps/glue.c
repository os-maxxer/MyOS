#include <solis/syscall.h>
#include <solis/spx.h>
#include <stdbool.h>

static const struct syscall_table *SYS = (const struct syscall_table *)SYSCALL_TABLE_ADDR;

void graphics_fill_rect(int x, int y, int w, int h, uint32_t color) {
    SYS->fill_rect(x, y, w, h, color);
}
void graphics_draw_rect(int x, int y, int w, int h, uint32_t color) {
    SYS->draw_rect(x, y, w, h, color);
}
void graphics_draw_string(int x, int y, const char *text, uint32_t color) {
    SYS->draw_string(x, y, text, color);
}
void graphics_put_pixel(int x, int y, uint32_t color) {
    SYS->put_pixel(x, y, color);
}
uint32_t graphics_get_pixel(int x, int y) {
    return SYS->get_pixel(x, y);
}
uint32_t graphics_get_width(void) {
    return SYS->get_width();
}
uint32_t graphics_get_height(void) {
    return SYS->get_height();
}
uint32_t timer_get_ticks(void) {
    return SYS->get_ticks();
}
void outb(uint16_t port, uint8_t value) {
    SYS->outb(port, value);
}

const char* vfs_get_cwd(void) {
    return SYS->vfs_get_cwd();
}
int vfs_cd(const char *path) {
    return SYS->vfs_cd(path);
}
int vfs_mkdir(const char *path) {
    return SYS->vfs_mkdir(path);
}
int vfs_ls(char names[][24], int max) {
    return SYS->vfs_ls(names, max);
}
int vfs_ls_at(const char *path, char names[][24], int max) {
    return SYS->vfs_ls_at(path, names, max);
}
int vfs_open(const char *path) {
    return SYS->vfs_open(path);
}
int vfs_read(int fd, uint8_t *buf, uint32_t size) {
    return SYS->vfs_read(fd, buf, size);
}
int vfs_write(int fd, const uint8_t *buf, uint32_t size) {
    return SYS->vfs_write(fd, buf, size);
}
int vfs_create(const char *path) {
    return SYS->vfs_create(path);
}
int vfs_delete(const char *path) {
    return SYS->vfs_delete(path);
}
int vfs_get_size(int fd) {
    return SYS->vfs_get_size(fd);
}

int solfs_list(char names[][24], int max) {
    return SYS->solfs_list(names, max);
}
int solfs_create(const char *name) {
    return SYS->solfs_create(name);
}
int solfs_open(const char *name) {
    return SYS->solfs_open(name);
}
int solfs_read(int fd, uint8_t *buf, uint32_t size) {
    return SYS->solfs_read(fd, buf, size);
}
int solfs_write(int fd, const uint8_t *buf, uint32_t size) {
    return SYS->solfs_write(fd, buf, size);
}
int solfs_get_size(int fd) {
    return SYS->solfs_get_size(fd);
}
uint64_t solfs_get_machine_id(void) {
    return SYS->solfs_get_machine_id();
}

uint32_t sys_get_total_ram(void) {
    return SYS->sys_get_total_ram();
}
void sys_get_cpu_brand(char *buf, int max_len) {
    SYS->sys_get_cpu_brand(buf, max_len);
}
uint64_t sys_get_machine_id(void) {
    return SYS->sys_get_machine_id();
}

int spx_install(const uint8_t *data, uint32_t size) {
    return SYS->spx_install(data, size);
}
int spx_uninstall(const char *name) {
    return SYS->spx_uninstall(name);
}
int spx_find_slot(const char *name) {
    return SYS->spx_find_slot(name);
}
int spx_list_installed(char names[][SPX_NAME_LEN], int max) {
    return SYS->spx_list_installed(names, max);
}

int gui_get_theme(void) {
    return SYS->gui_get_theme();
}
void gui_set_bg_color(uint32_t color) {
    SYS->gui_set_bg_color(color);
}
void gui_set_theme(int theme) {
    SYS->gui_set_theme(theme);
}

int ata_present(void) {
    return SYS->ata_present();
}
const char* ata_get_serial(void) {
    return SYS->ata_get_serial();
}

int net_available(void) {
    return SYS->net_available();
}
int net_http_get(const uint8_t *ip, uint16_t port,
                 const char *host, const char *path,
                 uint8_t *response, uint32_t max_size) {
    return SYS->net_http_get(ip, port, host, path, response, max_size);
}

int gui_launch_app(int slot) {
    return SYS->gui_launch_app(slot);
}

int net_ping(const uint8_t *ip, uint32_t timeout_ms) {
    return SYS->net_ping(ip, timeout_ms);
}

int net_arp_resolve(const uint8_t *ip, uint8_t *mac) {
    return SYS->net_arp_resolve(ip, mac);
}

int net_dns_resolve(const char *hostname, uint8_t *ip_out) {
    return SYS->net_dns_resolve(hostname, ip_out);
}

int dbg_read(char *buf, int max) {
    return SYS->dbg_read(buf, max);
}

void graphics_fill_circle(int cx, int cy, int r, uint32_t color) {
    SYS->fill_circle(cx, cy, r, color);
}

void rtc_get_time(struct rtc_time *out) {
    SYS->rtc_get_time(out);
}
void rtc_set_timezone(int index) {
    SYS->rtc_set_timezone(index);
}
int rtc_get_timezone(void) {
    return SYS->rtc_get_timezone();
}
int rtc_get_timezone_count(void) {
    return SYS->rtc_get_timezone_count();
}
void rtc_get_timezone_name(int index, char *buf, int max_len) {
    SYS->rtc_get_timezone_name(index, buf, max_len);
}
int rtc_get_timezone_offset(int index) {
    return SYS->rtc_get_timezone_offset(index);
}

int gui_save_dialog(const char *suggested, char *out_path, int out_max) {
    return SYS->gui_save_dialog(suggested, out_path, out_max);
}

void _start(void) {
    for (;;);
}

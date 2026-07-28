#include <nyx/npx.h>
#include <nyx/syscall.h>
#include <nyx/nofs.h>
#include <nyx/console.h>
#include <nyx/graphics.h>
#include <nyx/gui.h>
#include <nyx/vfs.h>
#include <nyx/ata.h>
#include <nyx/timer.h>
#include <nyx/ports.h>
#include <nyx/multiboot2.h>
#include <nyx/net.h>
#include <nyx/dbg.h>
#include <stddef.h>

static void npx_memcpy(void *dst, const void *src, int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}


static struct {
    bool loaded;
    struct app_exports exports;
} app_slots[NPX_MAX_APPS];

static struct syscall_table sys_table;

static uint32_t total_ram_mb = 0;
static char cpu_brand[48] = "Unknown CPU";

static void npx_exit(int code) {
    (void)code;
    for (;;) { __asm__ volatile("cli; hlt"); }
}

static int npx_ata_present(void) {
    return ata_present() ? 1 : 0;
}

static void do_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

void sys_init_ram(uint32_t multiboot_info) {
    uint32_t *info = (uint32_t *)multiboot_info;
    if (!info) { total_ram_mb = 256; return; }
    uint32_t total_size = info[0];
    uint8_t *ptr = (uint8_t *)(info + 2);
    uint64_t total = 0;
    while ((uint32_t)(ptr - (uint8_t *)info) < total_size) {
        uint32_t tag_type = *(uint32_t *)ptr;
        uint32_t tag_size = *(uint32_t *)(ptr + 4);
        if (tag_type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap = (struct multiboot_tag_mmap *)ptr;
            int count = (mmap->size - 16) / mmap->entry_size;
            for (int i = 0; i < count; i++) {
                struct multiboot_tag_mmap_entry *e = &mmap->entries[i];
                if (e->type == 1)
                    total += e->length;
            }
            break;
        }
        if (tag_type == 0) break;
        ptr += (tag_size + 7) & ~7U;
    }
    if (total == 0) total = 256UL * 1024 * 1024;
    total_ram_mb = (uint32_t)(total / (1024 * 1024));
}

void sys_init_cpu(void) {
    uint32_t a, b, c, d;
    do_cpuid(0, &a, &b, &c, &d);
    if (a >= 1) {
        npx_memcpy(cpu_brand + 0, &b, 4);
        npx_memcpy(cpu_brand + 4, &d, 4);
        npx_memcpy(cpu_brand + 8, &c, 4);
        cpu_brand[12] = '\0';
    }
    uint32_t max_ext = 0;
    do_cpuid(0x80000000, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000004) {
        do_cpuid(0x80000002, &a, &b, &c, &d);
        npx_memcpy(cpu_brand, &a, 4); npx_memcpy(cpu_brand + 4, &b, 4);
        npx_memcpy(cpu_brand + 8, &c, 4); npx_memcpy(cpu_brand + 12, &d, 4);
        do_cpuid(0x80000003, &a, &b, &c, &d);
        npx_memcpy(cpu_brand + 16, &a, 4); npx_memcpy(cpu_brand + 20, &b, 4);
        npx_memcpy(cpu_brand + 24, &c, 4); npx_memcpy(cpu_brand + 28, &d, 4);
        do_cpuid(0x80000004, &a, &b, &c, &d);
        npx_memcpy(cpu_brand + 32, &a, 4); npx_memcpy(cpu_brand + 36, &b, 4);
        npx_memcpy(cpu_brand + 40, &c, 4); npx_memcpy(cpu_brand + 44, &d, 4);
        cpu_brand[47] = '\0';
    }
    for (int i = 47; i >= 0; i--) {
        if (cpu_brand[i] == ' ') cpu_brand[i] = '\0';
        else if (cpu_brand[i] != '\0') break;
    }
}

uint32_t sys_get_total_ram(void) {
    return total_ram_mb;
}

void sys_get_cpu_brand(char *buf, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && cpu_brand[i]; i++)
        buf[i] = cpu_brand[i];
    buf[i] = '\0';
}

uint64_t sys_get_machine_id(void) {
    return nofs_get_machine_id();
}

void npx_init_syscalls(void) {
    sys_table.exit           = npx_exit;
    sys_table.fill_rect      = graphics_fill_rect;
    sys_table.draw_rect      = graphics_draw_rect;
    sys_table.draw_string    = graphics_draw_string;
    sys_table.put_pixel      = graphics_put_pixel;
    sys_table.get_pixel      = graphics_get_pixel;
    sys_table.get_width      = graphics_get_width;
    sys_table.get_height     = graphics_get_height;
    sys_table.get_ticks      = timer_get_ticks;
    sys_table.outb           = outb;
    sys_table.vfs_get_cwd    = vfs_get_cwd;
    sys_table.vfs_cd         = vfs_cd;
    sys_table.vfs_mkdir      = vfs_mkdir;
    sys_table.vfs_ls         = vfs_ls;
    sys_table.vfs_ls_at      = vfs_ls_at;
    sys_table.vfs_open       = vfs_open;
    sys_table.vfs_read       = vfs_read;
    sys_table.vfs_write      = vfs_write;
    sys_table.vfs_create     = vfs_create;
    sys_table.vfs_delete     = vfs_delete;
    sys_table.vfs_get_size   = vfs_get_size;
    sys_table.nofs_list      = nofs_list;
    sys_table.nofs_create    = nofs_create;
    sys_table.nofs_open      = nofs_open;
    sys_table.nofs_read      = nofs_read;
    sys_table.nofs_write     = nofs_write;
    sys_table.nofs_get_size  = nofs_get_size;
    sys_table.nofs_get_machine_id = nofs_get_machine_id;
    sys_table.gui_get_theme  = gui_get_theme;
    sys_table.gui_set_bg_color = gui_set_bg_color;
    sys_table.gui_set_theme  = gui_set_theme;
    sys_table.ata_present    = npx_ata_present;
    sys_table.ata_get_serial = ata_get_serial;
    sys_table.sys_get_total_ram = sys_get_total_ram;
    sys_table.sys_get_cpu_brand = sys_get_cpu_brand;
    sys_table.npx_install    = npx_install;
    sys_table.npx_uninstall  = npx_uninstall;
    sys_table.npx_find_slot  = npx_find_slot;
    sys_table.npx_list_installed = npx_list_installed;
    sys_table.sys_get_machine_id = sys_get_machine_id;
    sys_table.net_available    = net_available;
    sys_table.net_http_get     = net_http_get;
    sys_table.gui_launch_app   = gui_launch_app;
    sys_table.net_ping         = net_ping;
    sys_table.net_arp_resolve  = net_arp_resolve;
    sys_table.net_dns_resolve  = net_dns_resolve;
    sys_table.dbg_read         = dbg_read;

    struct syscall_table *target = (struct syscall_table *)SYSCALL_TABLE_ADDR;
    *target = sys_table;
}

void npx_init(struct embedded_npx *apps, int count) {
    npx_init_syscalls();

    for (int i = 0; i < NPX_MAX_APPS; i++)
        app_slots[i].loaded = false;

    for (int i = 0; i < count && i < NPX_MAX_APPS; i++) {
        const uint8_t *data = apps[i].data;
        uint32_t size = apps[i].size;

        if (size < sizeof(struct npx_header)) {
            console_write("[NPX] ");
            console_write(apps[i].name ? apps[i].name : "?");
            console_write(" too small.\n");
            continue;
        }

        const struct npx_header *hdr = (const struct npx_header *)data;
        if (hdr->magic != NPX_MAGIC || hdr->version != NPX_VERSION) {
            console_write("[NPX] Bad header in ");
            console_write(apps[i].name ? apps[i].name : "?");
            console_write("\n");
            continue;
        }

        uint8_t *load_addr = (uint8_t *)(NPX_SLOT_BASE + NPX_SLOT_SIZE * (uint32_t)i);
        uint32_t code_size = hdr->code_size;
        uint32_t bss_size  = hdr->bss_size;
        uint32_t hdr_size  = sizeof(struct npx_header);

        if (hdr_size + code_size > size) {
            console_write("[NPX] Truncated: ");
            console_write(apps[i].name);
            console_write("\n");
            continue;
        }

        for (uint32_t j = 0; j < code_size && j < NPX_SLOT_SIZE; j++)
            load_addr[j] = data[hdr_size + j];

        for (uint32_t j = 0; j < bss_size && code_size + j < NPX_SLOT_SIZE; j++)
            load_addr[code_size + j] = 0;

        uint32_t exp_off = hdr->exports_offset;
        if (exp_off + sizeof(struct app_exports) > NPX_SLOT_SIZE) {
            console_write("[NPX] Bad exports in ");
            console_write(apps[i].name);
            console_write("\n");
            continue;
        }

        const struct app_exports *exp = (const struct app_exports *)(load_addr + exp_off);

        app_slots[i].loaded = true;
        app_slots[i].exports.name = exp->name;
        app_slots[i].exports.init = exp->init;
        app_slots[i].exports.draw = exp->draw;
        app_slots[i].exports.handle_key = exp->handle_key;
        app_slots[i].exports.handle_mouse = exp->handle_mouse;

        if (app_slots[i].exports.name) {
            console_write("[NPX] Loaded ");
            console_write(app_slots[i].exports.name);
            console_write("\n");
        }

        if (app_slots[i].exports.init)
            app_slots[i].exports.init();
    }
}

const struct app_exports *npx_get_exports(int slot) {
    if (slot < 0 || slot >= NPX_MAX_APPS || !app_slots[slot].loaded)
        return 0;
    return &app_slots[slot].exports;
}

static int npx_find_free_slot(void) {
    for (int i = 0; i < NPX_MAX_APPS; i++)
        if (!app_slots[i].loaded) return i;
    return -1;
}

static int npx_load_to_slot(int slot, const uint8_t *data, uint32_t size) {
    if (slot < 0 || slot >= NPX_MAX_APPS) return -1;
    if (size < sizeof(struct npx_header)) return -1;

    const struct npx_header *hdr = (const struct npx_header *)data;
    if (hdr->magic != NPX_MAGIC || hdr->version != NPX_VERSION) return -1;

    uint8_t *load_addr = (uint8_t *)(NPX_SLOT_BASE + NPX_SLOT_SIZE * (uint32_t)slot);
    uint32_t code_size = hdr->code_size;
    uint32_t bss_size  = hdr->bss_size;
    uint32_t hdr_size  = sizeof(struct npx_header);

    if (hdr_size + code_size > size) return -1;

    for (uint32_t j = 0; j < code_size && j < NPX_SLOT_SIZE; j++)
        load_addr[j] = data[hdr_size + j];

    for (uint32_t j = 0; j < bss_size && code_size + j < NPX_SLOT_SIZE; j++)
        load_addr[code_size + j] = 0;

    uint32_t exp_off = hdr->exports_offset;
    if (exp_off + sizeof(struct app_exports) > NPX_SLOT_SIZE) return -1;

    const struct app_exports *exp = (const struct app_exports *)(load_addr + exp_off);

    app_slots[slot].loaded = true;
    app_slots[slot].exports.name = exp->name;
    app_slots[slot].exports.init = exp->init;
    app_slots[slot].exports.draw = exp->draw;
    app_slots[slot].exports.handle_key = exp->handle_key;
    app_slots[slot].exports.handle_mouse = exp->handle_mouse;

    if (app_slots[slot].exports.init)
        app_slots[slot].exports.init();

    return slot;
}

int npx_install(const uint8_t *data, uint32_t size) {
    int slot = npx_find_free_slot();
    if (slot < 0) return -1;
    return npx_load_to_slot(slot, data, size);
}

int npx_uninstall(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < NPX_MAX_APPS; i++) {
        if (app_slots[i].loaded && app_slots[i].exports.name) {
            const char *n = app_slots[i].exports.name;
            int match = 1;
            const char *a = n, *b = name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a != *b) match = 0;
            if (match) {
                app_slots[i].loaded = false;
                app_slots[i].exports.name = 0;
                app_slots[i].exports.init = 0;
                app_slots[i].exports.draw = 0;
                app_slots[i].exports.handle_key = 0;
                app_slots[i].exports.handle_mouse = 0;
                return 0;
            }
        }
    }
    return -1;
}

int npx_find_slot(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < NPX_MAX_APPS; i++) {
        if (app_slots[i].loaded && app_slots[i].exports.name) {
            const char *n = app_slots[i].exports.name;
            int match = 1;
            const char *a = n, *b = name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a != *b) match = 0;
            if (match) return i;
        }
    }
    return -1;
}

int npx_list_installed(char names[][NPX_NAME_LEN], int max) {
    int count = 0;
    for (int i = 0; i < NPX_MAX_APPS && count < max; i++) {
        if (app_slots[i].loaded && app_slots[i].exports.name) {
            const char *n = app_slots[i].exports.name;
            int j;
            for (j = 0; j < NPX_NAME_LEN - 1 && n[j]; j++)
                names[count][j] = n[j];
            names[count][j] = '\0';
            count++;
        }
    }
    return count;
}

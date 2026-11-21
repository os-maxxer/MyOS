#include <solis/spx.h>
#include <solis/syscall.h>
#include <solis/solfs.h>
#include <solis/setup.h>
#include <solis/console.h>
#include <solis/graphics.h>
#include <solis/gui.h>
#include <solis/vfs.h>
#include <solis/ata.h>
#include <solis/timer.h>
#include <solis/ports.h>
#include <solis/multiboot2.h>
#include <solis/net.h>
#include <solis/dbg.h>
#include <solis/rtc.h>
#include <solis/jit.h>
#include <solis/audio.h>
#include <stddef.h>

extern const uint8_t doom_wad_start[];
extern const uint8_t doom_wad_end[];

static void spx_memcpy(void *dst, const void *src, int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}


static struct {
    bool loaded;
    struct app_exports exports;
} app_slots[SPX_MAX_APPS];

static struct syscall_table sys_table;

static uint32_t total_ram_mb = 0;
static char cpu_brand[48] = "Unknown CPU";

static void spx_exit(int code) {
    (void)code;
    for (;;) { __asm__ volatile("cli; hlt"); }
}

static int spx_ata_present(void) {
    return ata_present() ? 1 : 0;
}

static void do_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

void sys_init_ram(uintptr_t multiboot_info) {
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
        spx_memcpy(cpu_brand + 0, &b, 4);
        spx_memcpy(cpu_brand + 4, &d, 4);
        spx_memcpy(cpu_brand + 8, &c, 4);
        cpu_brand[12] = '\0';
    }
    uint32_t max_ext = 0;
    do_cpuid(0x80000000, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000004) {
        do_cpuid(0x80000002, &a, &b, &c, &d);
        spx_memcpy(cpu_brand, &a, 4); spx_memcpy(cpu_brand + 4, &b, 4);
        spx_memcpy(cpu_brand + 8, &c, 4); spx_memcpy(cpu_brand + 12, &d, 4);
        do_cpuid(0x80000003, &a, &b, &c, &d);
        spx_memcpy(cpu_brand + 16, &a, 4); spx_memcpy(cpu_brand + 20, &b, 4);
        spx_memcpy(cpu_brand + 24, &c, 4); spx_memcpy(cpu_brand + 28, &d, 4);
        do_cpuid(0x80000004, &a, &b, &c, &d);
        spx_memcpy(cpu_brand + 32, &a, 4); spx_memcpy(cpu_brand + 36, &b, 4);
        spx_memcpy(cpu_brand + 40, &c, 4); spx_memcpy(cpu_brand + 44, &d, 4);
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
    return solfs_get_machine_id();
}

void spx_init_syscalls(void) {
    sys_table.exit           = spx_exit;
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
    sys_table.solfs_list      = solfs_list;
    sys_table.solfs_create    = solfs_create;
    sys_table.solfs_open      = solfs_open;
    sys_table.solfs_read      = solfs_read;
    sys_table.solfs_write     = solfs_write;
    sys_table.solfs_get_size  = solfs_get_size;
    sys_table.solfs_get_machine_id = solfs_get_machine_id;
    sys_table.solfs_get_total_bytes = solfs_get_total_bytes;
    sys_table.solfs_get_free_bytes = solfs_get_free_bytes;
    sys_table.solfs_get_used_bytes = solfs_get_used_bytes;
    sys_table.setup_is_installed = setup_is_installed;
    sys_table.setup_change_password = setup_change_password;
    sys_table.setup_edition = setup_edition;
    sys_table.setup_is_guest = setup_is_guest;
    sys_table.blit_rgb_scaled = graphics_blit_rgb_scaled;
    sys_table.doom_wad = doom_wad_start;
    sys_table.doom_wad_size = (uint32_t)(doom_wad_end - doom_wad_start);
    sys_table.setup_pending_edition = setup_pending_edition;
    sys_table.setup_request_edition = setup_request_edition;
    sys_table.net_https_get   = net_https_get;
    sys_table.gui_get_theme  = gui_get_theme;
    sys_table.gui_set_bg_color = gui_set_bg_color;
    sys_table.gui_set_theme  = gui_set_theme;
    sys_table.setup_set_theme = setup_set_theme;
    sys_table.ata_present    = spx_ata_present;
    sys_table.ata_get_serial = ata_get_serial;
    sys_table.ata_get_model  = ata_get_model;
    sys_table.sys_get_total_ram = sys_get_total_ram;
    sys_table.sys_get_cpu_brand = sys_get_cpu_brand;
    sys_table.jit_execute    = jit_execute;
    sys_table.spx_install    = spx_install;
    sys_table.spx_uninstall  = spx_uninstall;
    sys_table.spx_find_slot  = spx_find_slot;
    sys_table.spx_list_installed = spx_list_installed;
    sys_table.sys_get_machine_id = sys_get_machine_id;
    sys_table.net_available    = net_available;
    sys_table.net_get_device_name = net_get_device_name;
    sys_table.net_wifi_adapter_present = net_wifi_adapter_present;
    sys_table.net_get_ip       = net_get_ip;
    sys_table.net_get_gateway  = net_get_gateway;
    sys_table.net_get_dns      = net_get_dns;
    sys_table.net_http_get     = net_http_get;
    sys_table.gui_launch_app   = gui_launch_app;
    sys_table.net_ping         = net_ping;
    sys_table.net_arp_resolve  = net_arp_resolve;
    sys_table.net_dns_resolve  = net_dns_resolve;
    sys_table.dbg_read         = dbg_read;
    sys_table.dbg_get_health   = dbg_get_health;
    sys_table.fill_circle      = graphics_fill_circle;
    sys_table.rtc_get_time     = rtc_get_time;
    sys_table.rtc_set_timezone = rtc_set_timezone;
    sys_table.rtc_get_timezone = rtc_get_timezone;
    sys_table.rtc_get_timezone_count = rtc_get_timezone_count;
    sys_table.rtc_get_timezone_name  = rtc_get_timezone_name;
    sys_table.rtc_get_timezone_offset = rtc_get_timezone_offset;
    sys_table.gui_save_dialog       = gui_save_dialog;
    sys_table.gui_open_dialog       = gui_open_dialog;
    sys_table.gui_open_with         = gui_open_with;
    sys_table.audio_available       = audio_available;
    sys_table.audio_play_tone       = audio_play_tone;
    sys_table.text_width            = graphics_text_width;
    sys_table.spx_get_exports       = spx_get_exports;
    sys_table.spx_slot_for_base     = spx_slot_for_base;
    sys_table.gui_app_window        = gui_app_window;
    sys_table.net_dl_begin          = net_dl_begin;
    sys_table.net_dl_step           = net_dl_step;
    sys_table.net_dl_abort          = net_dl_abort;

    struct syscall_table *target = (struct syscall_table *)SYSCALL_TABLE_ADDR;
    *target = sys_table;
}

void spx_init(struct embedded_spx *apps, int count) {
    spx_init_syscalls();

    for (int i = 0; i < SPX_MAX_APPS; i++)
        app_slots[i].loaded = false;

    for (int i = 0; i < count && i < SPX_MAX_APPS; i++) {
        const uint8_t *data = apps[i].data;
        uint32_t size = apps[i].size;

        if (size < sizeof(struct spx_header)) {
            console_write("[SPX] ");
            console_write(apps[i].name ? apps[i].name : "?");
            console_write(" too small.\n");
            continue;
        }

        const struct spx_header *hdr = (const struct spx_header *)data;
        if (hdr->magic != SPX_MAGIC ||
            (hdr->version != 1 && hdr->version != SPX_VERSION)) {
            console_write("[SPX] Bad header in ");
            console_write(apps[i].name ? apps[i].name : "?");
            console_write("\n");
            continue;
        }

        uint8_t *load_addr = (uint8_t *)(uintptr_t)(SPX_SLOT_BASE + SPX_SLOT_SIZE * (uint32_t)i);
        uint32_t code_size = hdr->code_size;
        uint32_t bss_size  = hdr->bss_size;
        uint32_t hdr_size  = sizeof(struct spx_header);

        if (hdr_size + code_size > size) {
            console_write("[SPX] Truncated: ");
            console_write(apps[i].name);
            console_write("\n");
            continue;
        }

        for (uint32_t j = 0; j < code_size && j < SPX_SLOT_SIZE; j++)
            load_addr[j] = data[hdr_size + j];

        for (uint32_t j = 0; j < bss_size && code_size + j < SPX_SLOT_SIZE; j++)
            load_addr[code_size + j] = 0;

        uint32_t exp_off = hdr->exports_offset;
        if (exp_off + sizeof(struct app_exports) > SPX_SLOT_SIZE) {
            console_write("[SPX] Bad exports in ");
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
        app_slots[i].exports.get_size = exp->get_size;
        app_slots[i].exports.handle_scroll = exp->handle_scroll;
        app_slots[i].exports.handle_context = exp->handle_context;
        app_slots[i].exports.open_file = exp->open_file;
        app_slots[i].exports.tick = hdr->version >= 2 ? exp->tick : 0;

        if (app_slots[i].exports.init)
            app_slots[i].exports.init();
    }
}

const struct app_exports *spx_get_exports(int slot) {
    if (slot < 0 || slot >= SPX_MAX_APPS || !app_slots[slot].loaded)
        return 0;
    return &app_slots[slot].exports;
}

static int spx_find_free_slot(void) {
    for (int i = 0; i < SPX_MAX_APPS; i++)
        if (!app_slots[i].loaded) return i;
    return -1;
}

int spx_slot_for_base(uint32_t base) {
    if (base < SPX_SLOT_BASE) return -1;
    uint32_t off = base - SPX_SLOT_BASE;
    if (off % SPX_SLOT_SIZE) return -1;
    uint32_t slot = off / SPX_SLOT_SIZE;
    if (slot >= SPX_MAX_APPS) return -1;
    return (int)slot;
}

static int spx_names_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Read the exported name out of a not-yet-loaded package image. The struct
 * holds linked addresses, so translate the name pointer back into a file
 * offset relative to the image's declared base. */
static const char *spx_image_name(const struct spx_header *hdr,
                                  const uint8_t *data, uint32_t size) {
    uint32_t hdr_size = sizeof(struct spx_header);
    if (hdr->exports_offset + sizeof(struct app_exports) > hdr->code_size ||
        hdr_size + hdr->exports_offset + sizeof(struct app_exports) > size)
        return 0;
    if (hdr->load_base == 0) return 0;
    const struct app_exports *exp =
        (const struct app_exports *)(data + hdr_size + hdr->exports_offset);
    uintptr_t name_addr = (uintptr_t)exp->name;
    if (name_addr < hdr->load_base) return 0;
    uintptr_t off = name_addr - hdr->load_base;
    if (off >= hdr->code_size) return 0;
    const char *name = (const char *)(data + hdr_size + off);
    if ((uint32_t)(hdr_size + off) >= size) return 0;
    for (uint32_t i = 0; i < SPX_NAME_LEN * 2; i++) {
        if (hdr_size + off + i >= size) return 0;
        if (name[i] == '\0') return name;
    }
    return 0;
}

static int spx_load_to_slot(int slot, const uint8_t *data, uint32_t size) {
    if (slot < 0 || slot >= SPX_MAX_APPS) return SPX_ERR_BAD;
    if (size < sizeof(struct spx_header)) return SPX_ERR_BAD;

    const struct spx_header *hdr = (const struct spx_header *)data;
    if (hdr->magic != SPX_MAGIC ||
        (hdr->version != 1 && hdr->version != SPX_VERSION)) return SPX_ERR_BAD;

    uint8_t *load_addr = (uint8_t *)(uintptr_t)(SPX_SLOT_BASE + SPX_SLOT_SIZE * (uint32_t)slot);
    uint32_t code_size = hdr->code_size;
    uint32_t bss_size  = hdr->bss_size;
    uint32_t hdr_size  = sizeof(struct spx_header);

    if (hdr_size + code_size > size) return SPX_ERR_BAD;
    /* Reject rather than silently truncate: a package that does not fit
     * would run with a clipped .text and fault far from the real cause. */
    if (code_size > SPX_SLOT_SIZE || bss_size > SPX_SLOT_SIZE - code_size)
        return SPX_ERR_BAD;

    for (uint32_t j = 0; j < code_size && j < SPX_SLOT_SIZE; j++)
        load_addr[j] = data[hdr_size + j];

    for (uint32_t j = 0; j < bss_size && code_size + j < SPX_SLOT_SIZE; j++)
        load_addr[code_size + j] = 0;

    uint32_t exp_off = hdr->exports_offset;
    if (exp_off + sizeof(struct app_exports) > SPX_SLOT_SIZE) return SPX_ERR_BAD;

    const struct app_exports *exp = (const struct app_exports *)(load_addr + exp_off);

    app_slots[slot].loaded = true;
    app_slots[slot].exports.name = exp->name;
    app_slots[slot].exports.init = exp->init;
    app_slots[slot].exports.draw = exp->draw;
    app_slots[slot].exports.handle_key = exp->handle_key;
    app_slots[slot].exports.handle_mouse = exp->handle_mouse;
    app_slots[slot].exports.get_size = exp->get_size;
    app_slots[slot].exports.handle_scroll = exp->handle_scroll;
    app_slots[slot].exports.handle_context = exp->handle_context;
    app_slots[slot].exports.open_file = exp->open_file;
    app_slots[slot].exports.tick = hdr->version >= 2 ? exp->tick : 0;

    if (app_slots[slot].exports.init)
        app_slots[slot].exports.init();

    return slot;
}

int spx_install(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(struct spx_header)) return SPX_ERR_BAD;
    const struct spx_header *hdr = (const struct spx_header *)data;
    if (hdr->magic != SPX_MAGIC ||
        (hdr->version != 1 && hdr->version != SPX_VERSION)) return SPX_ERR_BAD;

    int slot = -1;
    if (hdr->version >= 3 && hdr->load_base) {
        /* Pinned package: the code was linked for one base, so it can only
         * run in that slot. Upgrading the app already there is allowed;
         * evicting a different app is not, because that would silently
         * replace whatever the user was running. */
        slot = spx_slot_for_base(hdr->load_base);
        if (slot < 0) return SPX_ERR_BASE;
        if (app_slots[slot].loaded) {
            const char *owner = app_slots[slot].exports.name;
            const char *image_name = spx_image_name(hdr, data, size);
            if (!spx_names_equal(owner, hdr->name) &&
                !spx_names_equal(owner, image_name))
                return SPX_ERR_BUSY;
        }
    } else {
        slot = spx_find_free_slot();
        if (slot < 0) return SPX_ERR_BUSY;
    }
    return spx_load_to_slot(slot, data, size);
}

int spx_uninstall(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SPX_MAX_APPS; i++) {
        if (app_slots[i].loaded && app_slots[i].exports.name) {
            const char *n = app_slots[i].exports.name;
            int match = 1;
            const char *a = n, *b = name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a != *b) match = 0;
            if (match) {
                /* Clear the header too, so a stale name in the slot image
                 * cannot be mistaken for a live app after removal. */
                uint8_t *addr = (uint8_t *)(uintptr_t)(SPX_SLOT_BASE + SPX_SLOT_SIZE * (uint32_t)i);
                for (uint32_t k = 0; k < sizeof(struct app_exports) && k < SPX_SLOT_SIZE; k++)
                    addr[k] = 0;
                app_slots[i].loaded = false;
                app_slots[i].exports.name = 0;
                app_slots[i].exports.init = 0;
                app_slots[i].exports.draw = 0;
                app_slots[i].exports.handle_key = 0;
                app_slots[i].exports.handle_mouse = 0;
                app_slots[i].exports.get_size = 0;
                app_slots[i].exports.tick = 0;
                return 0;
            }
        }
    }
    return -1;
}

int spx_find_slot(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SPX_MAX_APPS; i++) {
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

int spx_list_installed(char names[][SPX_NAME_LEN], int max) {
    int count = 0;
    for (int i = 0; i < SPX_MAX_APPS && count < max; i++) {
        if (app_slots[i].loaded && app_slots[i].exports.name) {
            const char *n = app_slots[i].exports.name;
            int j;
            for (j = 0; j < SPX_NAME_LEN - 1 && n[j]; j++)
                names[count][j] = n[j];
            names[count][j] = '\0';
            count++;
        }
    }
    return count;
}

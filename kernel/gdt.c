/*
 * Global descriptor table setup for the x86-64 kernel.
 */

#include <solis/gdt.h>
#include <solis/tss.h>

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint64_t gdt[5];
static struct gdt_ptr gp;

extern void load_gdt(const struct gdt_ptr *gdt_ptr);
extern void load_tss(void);

static void gdt_set_tss(uint64_t base, uint32_t limit) {
    gdt[3] = (limit & 0xFFFFU) |
             ((base & 0xFFFFFFULL) << 16) |
             (0x89ULL << 40) |
             (((uint64_t)(limit >> 16) & 0x0FULL) << 48) |
             (((base >> 24) & 0xFFULL) << 56);
    gdt[4] = base >> 32;
}

void gdt_init(void) {
    tss_init();
    const struct x86_64_tss *tss = tss_get();

    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;
    gdt_set_tss((uintptr_t)tss, sizeof(*tss) - 1);
    gp.limit = (sizeof(gdt) - 1);
    gp.base = (uint64_t)(uintptr_t)&gdt;

    load_gdt(&gp);
    load_tss();
}

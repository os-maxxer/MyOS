#include <solis/jit.h>
#include <solis/vmm.h>
#include <solis/pmm.h>
#include <solis/console.h>
#include <stddef.h>

#define JIT_PAGE_SIZE 4096U
#define JIT_MAX_CODE_PAGES (SOLIS_JIT_MAX_CODE / JIT_PAGE_SIZE)
#define JIT_MAX_DATA_PAGES (SOLIS_JIT_MAX_DATA / JIT_PAGE_SIZE)
#define JIT_PAGE_MASK (JIT_PAGE_SIZE - 1U)

static void jit_copy(uint8_t *destination, const uint8_t *source, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) destination[i] = source[i];
}

static bool jit_map_window(uintptr_t base, uint32_t pages, uint64_t flags) {
    for (uint32_t i = 0; i < pages; i++) {
        void *page = pmm_alloc_page();
        if (!page) return false;
        if (!vmm_map_page(base + i * JIT_PAGE_SIZE,
                          (uint64_t)(uintptr_t)page, flags)) {
            pmm_free_page(page);
            return false;
        }
        jit_copy((uint8_t *)(base + i * JIT_PAGE_SIZE),
                 (const uint8_t *)page, JIT_PAGE_SIZE);
    }
    return true;
}

static void jit_unmap_window(uintptr_t base, uint32_t pages) {
    for (uint32_t i = 0; i < pages; i++) {
        uintptr_t address = base + i * JIT_PAGE_SIZE;
        uint64_t physical = 0;
        if (!vmm_translate(address, &physical)) continue;
        if (vmm_unmap_page(address)) {
            pmm_free_page((void *)(uintptr_t)(physical & ~((uint64_t)JIT_PAGE_MASK)));
        }
    }
}

int jit_execute(const uint8_t *code, uint32_t code_size,
                const uint8_t *data, uint32_t data_size) {
    if (!code || code_size == 0 || code_size > SOLIS_JIT_MAX_CODE ||
        data_size > SOLIS_JIT_MAX_DATA)
        return -1;

    uint32_t code_pages = (code_size + JIT_PAGE_MASK) / JIT_PAGE_SIZE;
    uint32_t data_pages = data_size ? (data_size + JIT_PAGE_MASK) / JIT_PAGE_SIZE : 0;
    if (code_pages > JIT_MAX_CODE_PAGES || data_pages > JIT_MAX_DATA_PAGES)
        return -1;

    if (!jit_map_window(SOLIS_JIT_CODE_BASE, code_pages, VMM_PAGE_WRITABLE)) {
        jit_unmap_window(SOLIS_JIT_CODE_BASE, code_pages);
        return -1;
    }
    jit_copy((uint8_t *)SOLIS_JIT_CODE_BASE, code, code_size);

    if (data_pages && data) {
        if (!jit_map_window(SOLIS_JIT_DATA_BASE, data_pages, VMM_PAGE_WRITABLE)) {
            jit_unmap_window(SOLIS_JIT_DATA_BASE, data_pages);
            jit_unmap_window(SOLIS_JIT_CODE_BASE, code_pages);
            return -1;
        }
        jit_copy((uint8_t *)SOLIS_JIT_DATA_BASE, data, data_size);
    }

    uint64_t data_flags = VMM_PAGE_WRITABLE;
    if (vmm_nx_enabled()) data_flags |= VMM_PAGE_NO_EXECUTE;
    if (!vmm_protect_range(SOLIS_JIT_CODE_BASE,
                           SOLIS_JIT_CODE_BASE + code_pages * JIT_PAGE_SIZE, 0) ||
        (data_pages && !vmm_protect_range(SOLIS_JIT_DATA_BASE,
                                          SOLIS_JIT_DATA_BASE + data_pages * JIT_PAGE_SIZE,
                                          data_flags))) {
        jit_unmap_window(SOLIS_JIT_DATA_BASE, data_pages);
        jit_unmap_window(SOLIS_JIT_CODE_BASE, code_pages);
        return -1;
    }

    int (*entry)(void) = (int (*)(void))(uintptr_t)SOLIS_JIT_CODE_BASE;
    int status = entry();

    jit_unmap_window(SOLIS_JIT_DATA_BASE, data_pages);
    jit_unmap_window(SOLIS_JIT_CODE_BASE, code_pages);
    return status;
}

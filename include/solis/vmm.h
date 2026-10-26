#ifndef SOLIS_VMM_H
#define SOLIS_VMM_H

#include <solis/types.h>
#include <stdbool.h>

#define VMM_PAGE_PRESENT       (1ULL << 0)
#define VMM_PAGE_WRITABLE      (1ULL << 1)
#define VMM_PAGE_USER          (1ULL << 2)
#define VMM_PAGE_WRITE_THROUGH (1ULL << 3)
#define VMM_PAGE_CACHE_DISABLE (1ULL << 4)
#define VMM_PAGE_GLOBAL        (1ULL << 8)
#define VMM_PAGE_NO_EXECUTE    (1ULL << 63)

bool vmm_init(void);
bool vmm_map_page(uintptr_t virtual_address, uint64_t physical_address, uint64_t flags);
bool vmm_unmap_page(uintptr_t virtual_address);
bool vmm_translate(uintptr_t virtual_address, uint64_t *physical_address);
bool vmm_protect_range(uintptr_t start, uintptr_t end, uint64_t flags);

#endif

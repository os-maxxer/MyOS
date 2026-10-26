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

struct vmm_address_space {
    uint64_t *root;
};

#define VMM_USER_SPACE_START 0x0000010000000000ULL
#define VMM_USER_SPACE_END   0x0000800000000000ULL

/* Address-space structs must be zero-initialized before creation. */
bool vmm_init(void);
bool vmm_nx_enabled(void);
bool vmm_map_page(uintptr_t virtual_address, uint64_t physical_address, uint64_t flags);
bool vmm_unmap_page(uintptr_t virtual_address);
bool vmm_translate(uintptr_t virtual_address, uint64_t *physical_address);
bool vmm_protect_range(uintptr_t start, uintptr_t end, uint64_t flags);
bool vmm_address_space_create(struct vmm_address_space *space);
bool vmm_address_space_activate(struct vmm_address_space *space);
bool vmm_address_space_destroy(struct vmm_address_space *space);
void vmm_activate_kernel(void);
/* Returns the zeroed physical page mapped at the requested user virtual page. */
void *vmm_address_space_map_alloc(struct vmm_address_space *space,
                                  uintptr_t virtual_address, uint64_t flags);
bool vmm_copy_from_user(void *destination, uintptr_t source, size_t size);

#endif

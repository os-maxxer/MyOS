#ifndef SOLIS_PMM_H
#define SOLIS_PMM_H

#include <solis/types.h>
#include <stdbool.h>

#define PMM_PAGE_SIZE 4096
#define PMM_DIRECT_MAP_LIMIT (512ULL * 1024 * 1024 * 1024)

bool pmm_init(uintptr_t multiboot_info);
void *pmm_alloc_page(void);
bool pmm_free_page(void *page);
/* Usable RAM managed below the boot identity-map limit, including reservations. */
uint64_t pmm_total_bytes(void);
/* Pages currently available to pmm_alloc_page. */
uint64_t pmm_free_bytes(void);

#endif

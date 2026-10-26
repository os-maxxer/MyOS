#include <solis/vmm.h>
#include <solis/pmm.h>
#include <stdbool.h>

#define PAGE_SIZE  4096ULL
#define PAGE_MASK  (PAGE_SIZE - 1)
#define ENTRY_COUNT 512
#define ENTRY_PRESENT (1ULL << 0)
#define ENTRY_WRITABLE (1ULL << 1)
#define ENTRY_USER (1ULL << 2)
#define ENTRY_WRITE_THROUGH (1ULL << 3)
#define ENTRY_CACHE_DISABLE (1ULL << 4)
#define ENTRY_ACCESSED (1ULL << 5)
#define ENTRY_DIRTY (1ULL << 6)
#define ENTRY_LARGE (1ULL << 7)
#define ENTRY_GLOBAL (1ULL << 8)
#define ENTRY_LARGE_PAT (1ULL << 12)
#define ENTRY_NO_EXECUTE (1ULL << 63)
#define ENTRY_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define ENTRY_1G_ADDRESS_MASK 0x000FFFFFC0000000ULL
#define ENTRY_2M_ADDRESS_MASK 0x000FFFFFFFE00000ULL
#define VMM_IDENTITY_LIMIT PMM_DIRECT_MAP_LIMIT
#define VMM_SELF_TEST_ADDRESS 0xFFFF900000000000ULL

#define VMM_ALLOWED_FLAGS (VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER | \
                           VMM_PAGE_WRITE_THROUGH | VMM_PAGE_CACHE_DISABLE | \
                           VMM_PAGE_GLOBAL | VMM_PAGE_NO_EXECUTE)

static uint64_t *page_root;
static uint64_t physical_address_mask = ENTRY_ADDRESS_MASK;
static bool nx_supported;

extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __spx_apps_start[];
extern char __spx_apps_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];
extern char __frame_stage_start[];
extern char __frame_stage_end[];

static void vmm_cpuid(uint32_t leaf, uint32_t subleaf,
                      uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(subleaf));
}

static bool vmm_is_canonical(uint64_t address) {
    uint64_t upper = address >> 48;
    return ((address >> 47) & 1) ? upper == 0xFFFF : upper == 0;
}

static bool vmm_valid_range(uint64_t start, uint64_t end) {
    if (start >= end || !vmm_is_canonical(start) ||
        !vmm_is_canonical(end - 1))
        return false;
    return ((start >> 47) & 1) == (((end - 1) >> 47) & 1);
}

static bool vmm_valid_physical(uint64_t address) {
    return (address & PAGE_MASK) == 0 &&
           (address & ~physical_address_mask) == 0;
}

static uint64_t *vmm_table_from_entry(uint64_t entry) {
    uint64_t address = entry & physical_address_mask;
    if (address >= VMM_IDENTITY_LIMIT) return 0;
    return (uint64_t *)(uintptr_t)address;
}

static uint64_t *vmm_new_table(uint64_t *parent, uint32_t index, bool user) {
    uint64_t *table = (uint64_t *)pmm_alloc_page();
    if (!table) return 0;
    parent[index] = ((uint64_t)(uintptr_t)table & physical_address_mask) |
                    ENTRY_PRESENT | ENTRY_WRITABLE | (user ? ENTRY_USER : 0);
    return table;
}

static bool vmm_split_1g(uint64_t *entry_ptr) {
    uint64_t old = *entry_ptr;
    if (!(old & ENTRY_LARGE)) return true;

    uint64_t *directory = (uint64_t *)pmm_alloc_page();
    if (!directory) return false;
    uint64_t base = old & ENTRY_1G_ADDRESS_MASK;
    uint64_t flags = old & (ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER |
                            ENTRY_WRITE_THROUGH | ENTRY_CACHE_DISABLE |
                            ENTRY_ACCESSED | ENTRY_DIRTY | ENTRY_GLOBAL |
                            ENTRY_NO_EXECUTE);
    if (old & ENTRY_LARGE_PAT) flags |= ENTRY_LARGE_PAT;
    for (uint32_t i = 0; i < ENTRY_COUNT; i++)
        directory[i] = (base + (uint64_t)i * (1ULL << 21)) |
                       flags | ENTRY_LARGE;

    *entry_ptr = ((uint64_t)(uintptr_t)directory & physical_address_mask) |
                 ENTRY_PRESENT | ENTRY_WRITABLE |
                 (old & (ENTRY_USER | ENTRY_NO_EXECUTE));
    return true;
}

static bool vmm_split_2m(uint64_t *entry_ptr) {
    uint64_t old = *entry_ptr;
    if (!(old & ENTRY_LARGE)) return true;

    uint64_t *table = (uint64_t *)pmm_alloc_page();
    if (!table) return false;
    uint64_t base = old & ENTRY_2M_ADDRESS_MASK;
    uint64_t flags = old & (ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER |
                            ENTRY_WRITE_THROUGH | ENTRY_CACHE_DISABLE |
                            ENTRY_ACCESSED | ENTRY_DIRTY | ENTRY_GLOBAL |
                            ENTRY_NO_EXECUTE);
    if (old & ENTRY_LARGE_PAT) flags |= ENTRY_LARGE;
    for (uint32_t i = 0; i < ENTRY_COUNT; i++)
        table[i] = (base + (uint64_t)i * PAGE_SIZE) | flags;

    *entry_ptr = ((uint64_t)(uintptr_t)table & physical_address_mask) |
                 ENTRY_PRESENT | ENTRY_WRITABLE |
                 (old & (ENTRY_USER | ENTRY_NO_EXECUTE));
    return true;
}

static uint64_t *vmm_get_leaf(uint64_t address, bool create, bool user) {
    uint32_t pml4_index = (uint32_t)((address >> 39) & 0x1FF);
    uint32_t pdpt_index = (uint32_t)((address >> 30) & 0x1FF);
    uint32_t pd_index = (uint32_t)((address >> 21) & 0x1FF);
    uint32_t pt_index = (uint32_t)((address >> 12) & 0x1FF);

    uint64_t *pdpt;
    if (!(page_root[pml4_index] & ENTRY_PRESENT)) {
        if (!create) return 0;
        pdpt = vmm_new_table(page_root, pml4_index, user);
    } else {
        page_root[pml4_index] |= ENTRY_WRITABLE | (user ? ENTRY_USER : 0);
        pdpt = vmm_table_from_entry(page_root[pml4_index]);
    }
    if (!pdpt) return 0;

    uint64_t *pdpte = &pdpt[pdpt_index];
    uint64_t *pd;
    if ((*pdpte & (ENTRY_PRESENT | ENTRY_LARGE)) ==
        (ENTRY_PRESENT | ENTRY_LARGE)) {
        if (!create || !vmm_split_1g(pdpte)) return 0;
        pd = vmm_table_from_entry(*pdpte);
    } else if (!(*pdpte & ENTRY_PRESENT)) {
        if (!create) return 0;
        pd = vmm_new_table(pdpt, pdpt_index, user);
    } else {
        *pdpte |= ENTRY_WRITABLE | (user ? ENTRY_USER : 0);
        pd = vmm_table_from_entry(*pdpte);
    }
    if (!pd) return 0;

    uint64_t *pde = &pd[pd_index];
    if ((*pde & (ENTRY_PRESENT | ENTRY_LARGE)) ==
        (ENTRY_PRESENT | ENTRY_LARGE)) {
        if (!create || !vmm_split_2m(pde)) return 0;
    } else if (!(*pde & ENTRY_PRESENT)) {
        if (!create) return 0;
        uint64_t *pt = vmm_new_table(pd, pd_index, user);
        if (!pt) return 0;
        pde = &pd[pd_index];
        if (!(*pde & ENTRY_PRESENT)) return 0;
    } else {
        *pde |= ENTRY_WRITABLE | (user ? ENTRY_USER : 0);
    }

    uint64_t *pt = vmm_table_from_entry(*pde);
    if (!pt) return 0;
    uint64_t *pte = &pt[pt_index];
    if (!create && !(*pte & ENTRY_PRESENT)) return 0;
    return pte;
}

static void vmm_invalidate(uintptr_t address) {
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}

static bool vmm_check_page_flags(uintptr_t address, uint64_t must_set,
                                 uint64_t must_clear) {
    uint64_t *pte = vmm_get_leaf(address, false, false);
    return pte && (*pte & must_set) == must_set && (*pte & must_clear) == 0;
}

bool vmm_init(void) {
    uint32_t a, b, c, d;
    vmm_cpuid(0x80000000, 0, &a, &b, &c, &d);
    uint32_t max_extended_leaf = a;
    if (max_extended_leaf >= 0x80000001) {
        vmm_cpuid(0x80000001, 0, &a, &b, &c, &d);
        nx_supported = (d & (1U << 20)) != 0;
    }
    if (max_extended_leaf >= 0x80000008) {
        vmm_cpuid(0x80000008, 0, &a, &b, &c, &d);
        uint32_t physical_bits = a & 0xFF;
        if (physical_bits < 32 || physical_bits > 52) return false;
        physical_address_mask =
            ((1ULL << physical_bits) - 1) & ENTRY_ADDRESS_MASK;
    }

    if (nx_supported) {
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
        lo |= 1U << 11;
        __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0xC0000080));
    }

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t root_address = cr3 & physical_address_mask;
    if ((root_address & PAGE_MASK) != 0 || root_address >= VMM_IDENTITY_LIMIT)
        return false;
    page_root = (uint64_t *)(uintptr_t)root_address;

    uint64_t nx = nx_supported ? VMM_PAGE_NO_EXECUTE : 0;
    if (!vmm_protect_range((uintptr_t)__text_start, (uintptr_t)__text_end, 0) ||
        !vmm_protect_range((uintptr_t)__rodata_start, (uintptr_t)__rodata_end, nx) ||
        !vmm_protect_range((uintptr_t)__spx_apps_start, (uintptr_t)__spx_apps_end, nx) ||
        !vmm_protect_range((uintptr_t)__data_start, (uintptr_t)__data_end,
                           VMM_PAGE_WRITABLE | nx) ||
        !vmm_protect_range((uintptr_t)__bss_start, (uintptr_t)__bss_end,
                           VMM_PAGE_WRITABLE | nx) ||
        !vmm_protect_range((uintptr_t)__frame_stage_start, (uintptr_t)__frame_stage_end,
                           VMM_PAGE_WRITABLE | nx))
        return false;
    if (!vmm_check_page_flags((uintptr_t)__text_start, ENTRY_PRESENT,
                              ENTRY_WRITABLE | ENTRY_NO_EXECUTE) ||
        !vmm_check_page_flags((uintptr_t)__rodata_start,
                              ENTRY_PRESENT | (nx_supported ? ENTRY_NO_EXECUTE : 0),
                              ENTRY_WRITABLE) ||
        !vmm_check_page_flags((uintptr_t)__bss_start,
                              ENTRY_PRESENT | ENTRY_WRITABLE |
                                  (nx_supported ? ENTRY_NO_EXECUTE : 0),
                              0))
        return false;

    void *page = pmm_alloc_page();
    if (!page) return false;
    uint64_t physical = (uintptr_t)page;
    if (!vmm_map_page(VMM_SELF_TEST_ADDRESS, physical,
                      VMM_PAGE_WRITABLE | nx)) {
        pmm_free_page(page);
        return false;
    }
    volatile uint64_t *test = (volatile uint64_t *)(uintptr_t)VMM_SELF_TEST_ADDRESS;
    *test = 0x534F4C4953363442ULL;
    uint64_t translated;
    bool passed = *test == 0x534F4C4953363442ULL &&
                  vmm_translate(VMM_SELF_TEST_ADDRESS, &translated) &&
                  translated == physical &&
                  vmm_unmap_page(VMM_SELF_TEST_ADDRESS);
    uint64_t free_before_guard_tests = pmm_free_bytes();
    bool rejected_kernel_user_mapping =
        !vmm_map_page(VMM_SELF_TEST_ADDRESS, physical, VMM_PAGE_USER | nx);
    bool rejected_noncanonical_range =
        !vmm_protect_range(0x00007FFFFFFFF000ULL, 0xFFFF800000001000ULL, 0);
    bool guard_tests_preserved_memory =
        pmm_free_bytes() == free_before_guard_tests;
    bool freed = pmm_free_page(page);
    bool rejected_double_free = !pmm_free_page(page);
    if (!passed || !rejected_kernel_user_mapping ||
        !rejected_noncanonical_range || !guard_tests_preserved_memory ||
        !freed || !rejected_double_free)
        return false;
    return true;
}

bool vmm_map_page(uintptr_t virtual_address, uint64_t physical_address, uint64_t flags) {
    if (!page_root || (virtual_address & PAGE_MASK) != 0 ||
        !vmm_is_canonical(virtual_address) || !vmm_valid_physical(physical_address) ||
        (flags & ~VMM_ALLOWED_FLAGS) != 0 ||
        ((flags & VMM_PAGE_USER) && ((uint64_t)virtual_address >> 47) != 0) ||
        ((flags & VMM_PAGE_NO_EXECUTE) && !nx_supported))
        return false;

    uint64_t *pte = vmm_get_leaf(virtual_address, true, (flags & VMM_PAGE_USER) != 0);
    if (!pte) return false;
    *pte = physical_address | (flags & VMM_ALLOWED_FLAGS) | ENTRY_PRESENT;
    vmm_invalidate(virtual_address);
    return true;
}

bool vmm_unmap_page(uintptr_t virtual_address) {
    if (!page_root || (virtual_address & PAGE_MASK) != 0 ||
        !vmm_is_canonical(virtual_address))
        return false;
    uint64_t existing_physical;
    if (!vmm_translate(virtual_address, &existing_physical)) return false;
    uint64_t *pte = vmm_get_leaf(virtual_address, false, false);
    if (!pte || !(*pte & ENTRY_PRESENT)) return false;
    *pte = 0;
    vmm_invalidate(virtual_address);
    return true;
}

bool vmm_translate(uintptr_t virtual_address, uint64_t *physical_address) {
    if (!page_root || !physical_address || !vmm_is_canonical(virtual_address))
        return false;

    uint64_t entry = page_root[(virtual_address >> 39) & 0x1FF];
    if (!(entry & ENTRY_PRESENT)) return false;
    uint64_t *table = vmm_table_from_entry(entry);
    if (!table) return false;
    entry = table[(virtual_address >> 30) & 0x1FF];
    if (!(entry & ENTRY_PRESENT)) return false;
    if (entry & ENTRY_LARGE) {
        *physical_address = (entry & ENTRY_1G_ADDRESS_MASK) |
                            (virtual_address & ((1ULL << 30) - 1));
        return true;
    }

    table = vmm_table_from_entry(entry);
    if (!table) return false;
    entry = table[(virtual_address >> 21) & 0x1FF];
    if (!(entry & ENTRY_PRESENT)) return false;
    if (entry & ENTRY_LARGE) {
        *physical_address = (entry & ENTRY_2M_ADDRESS_MASK) |
                            (virtual_address & ((1ULL << 21) - 1));
        return true;
    }

    table = vmm_table_from_entry(entry);
    if (!table) return false;
    entry = table[(virtual_address >> 12) & 0x1FF];
    if (!(entry & ENTRY_PRESENT)) return false;
    *physical_address = (entry & physical_address_mask) |
                        (virtual_address & PAGE_MASK);
    return true;
}

bool vmm_protect_range(uintptr_t start, uintptr_t end, uint64_t flags) {
    if (start > end || (flags & ~VMM_ALLOWED_FLAGS) != 0 ||
        ((flags & VMM_PAGE_NO_EXECUTE) && !nx_supported))
        return false;
    if (start == end) return true;

    if (!vmm_valid_range(start, end)) return false;
    uint64_t first = (uint64_t)start & ~PAGE_MASK;
    uint64_t last = ((uint64_t)end + PAGE_MASK) & ~PAGE_MASK;
    if (last < (uint64_t)end || !vmm_is_canonical(first) ||
        !vmm_is_canonical(last - PAGE_SIZE) ||
        ((first >> 47) & 1) != (((last - PAGE_SIZE) >> 47) & 1))
        return false;

    for (uint64_t address = first; address < last; address += PAGE_SIZE) {
        uint64_t physical;
        if (!vmm_translate((uintptr_t)address, &physical) ||
            !vmm_map_page((uintptr_t)address, physical & ~PAGE_MASK, flags))
            return false;
    }
    return true;
}

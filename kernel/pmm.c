#include <solis/pmm.h>
#include <solis/multiboot2.h>
#include <stddef.h>

#define PMM_MAX_RANGES 1024
#define MULTIBOOT_TAG_TYPE_MODULE 3
#define MULTIBOOT_TAG_TYPE_END 0

struct pmm_range {
    uint64_t start;
    uint64_t end;
};

static struct pmm_range free_ranges[PMM_MAX_RANGES];
static struct pmm_range allocated_ranges[PMM_MAX_RANGES];
static uint32_t free_range_count;
static uint32_t allocated_range_count;
static uint64_t total_pages;
static uint64_t free_pages;

extern char __kernel_start[];
extern char __kernel_end[];

static uint64_t pmm_disable_interrupts(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void pmm_restore_interrupts(uint64_t flags) {
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

static bool pmm_insert_range(struct pmm_range *ranges, uint32_t *count,
                             uint64_t start, uint64_t end) {
    if (start >= end) return true;
    uint32_t pos = 0;
    while (pos < *count && ranges[pos].start < start) pos++;

    bool joins_left = pos > 0 && ranges[pos - 1].end >= start;
    bool joins_right = pos < *count && ranges[pos].start <= end;
    if (joins_left) {
        if (end > ranges[pos - 1].end) ranges[pos - 1].end = end;
        if (joins_right && ranges[pos].end > ranges[pos - 1].end)
            ranges[pos - 1].end = ranges[pos].end;
        if (joins_right) {
            for (uint32_t i = pos; i + 1 < *count; i++)
                ranges[i] = ranges[i + 1];
            (*count)--;
        }
        pos--;
    } else if (joins_right) {
        ranges[pos].start = start;
        if (end > ranges[pos].end) ranges[pos].end = end;
    } else {
        if (*count >= PMM_MAX_RANGES) return false;
        for (uint32_t i = *count; i > pos; i--)
            ranges[i] = ranges[i - 1];
        ranges[pos].start = start;
        ranges[pos].end = end;
        (*count)++;
    }

    while (pos + 1 < *count && ranges[pos].end >= ranges[pos + 1].start) {
        if (ranges[pos + 1].end > ranges[pos].end)
            ranges[pos].end = ranges[pos + 1].end;
        for (uint32_t i = pos + 1; i + 1 < *count; i++)
            ranges[i] = ranges[i + 1];
        (*count)--;
    }
    return true;
}

static bool pmm_add_range(uint64_t start, uint64_t end) {
    return pmm_insert_range(free_ranges, &free_range_count, start, end);
}

static bool pmm_range_can_add(const struct pmm_range *ranges, uint32_t count,
                              uint64_t start, uint64_t end) {
    uint32_t merged = 0;
    for (uint32_t i = 0; i < count; i++)
        if (ranges[i].end >= start && ranges[i].start <= end) merged++;
    return merged != 0 || count < PMM_MAX_RANGES;
}

static bool pmm_range_contains(const struct pmm_range *ranges, uint32_t count,
                               uint64_t start, uint64_t end) {
    for (uint32_t i = 0; i < count; i++)
        if (ranges[i].start <= start && ranges[i].end >= end) return true;
    return false;
}

static bool pmm_remove_allocated_range(uint64_t start, uint64_t end) {
    for (uint32_t i = 0; i < allocated_range_count; i++) {
        struct pmm_range range = allocated_ranges[i];
        if (start < range.start || end > range.end) continue;
        if (start == range.start && end == range.end) {
            for (uint32_t j = i; j + 1 < allocated_range_count; j++)
                allocated_ranges[j] = allocated_ranges[j + 1];
            allocated_range_count--;
        } else if (start == range.start) {
            allocated_ranges[i].start = end;
        } else if (end == range.end) {
            allocated_ranges[i].end = start;
        } else {
            if (allocated_range_count >= PMM_MAX_RANGES) return false;
            for (uint32_t j = allocated_range_count; j > i + 1; j--)
                allocated_ranges[j] = allocated_ranges[j - 1];
            allocated_ranges[i].end = start;
            allocated_ranges[i + 1].start = end;
            allocated_ranges[i + 1].end = range.end;
            allocated_range_count++;
        }
        return true;
    }
    return false;
}

static bool pmm_reserve_range(uint64_t start, uint64_t end) {
    if (start >= end) return true;
    start &= ~(uint64_t)(PMM_PAGE_SIZE - 1);
    if (end > PMM_DIRECT_MAP_LIMIT - (PMM_PAGE_SIZE - 1))
        end = PMM_DIRECT_MAP_LIMIT;
    else
        end = (end + PMM_PAGE_SIZE - 1) & ~(uint64_t)(PMM_PAGE_SIZE - 1);
    for (uint32_t i = 0; i < free_range_count;) {
        struct pmm_range range = free_ranges[i];
        if (end <= range.start || start >= range.end) {
            i++;
            continue;
        }

        uint64_t cut_start = start > range.start ? start : range.start;
        uint64_t cut_end = end < range.end ? end : range.end;
        uint64_t removed = (cut_end - cut_start) / PMM_PAGE_SIZE;
        free_pages -= removed;

        if (cut_start == range.start && cut_end == range.end) {
            for (uint32_t j = i; j + 1 < free_range_count; j++)
                free_ranges[j] = free_ranges[j + 1];
            free_range_count--;
            continue;
        }
        if (cut_start == range.start) {
            free_ranges[i].start = cut_end;
            i++;
            continue;
        }
        if (cut_end == range.end) {
            free_ranges[i].end = cut_start;
            i++;
            continue;
        }
        if (free_range_count >= PMM_MAX_RANGES) return false;
        for (uint32_t j = free_range_count; j > i + 1; j--)
            free_ranges[j] = free_ranges[j - 1];
        free_ranges[i].end = cut_start;
        free_ranges[i + 1].start = cut_end;
        free_ranges[i + 1].end = range.end;
        free_range_count++;
        i += 2;
    }
    return true;
}

static bool pmm_parse_multiboot(uintptr_t multiboot_info, uint32_t total_size) {
    const uint8_t *info = (const uint8_t *)multiboot_info;
    uint32_t offset = 8;
    bool found_map = false;

    while (total_size - offset >= 8) {
        const uint8_t *tag = info + offset;
        uint32_t type = *(const uint32_t *)tag;
        uint32_t size = *(const uint32_t *)(tag + 4);
        if (size < 8 || size > total_size - offset) return false;
        if (type == MULTIBOOT_TAG_TYPE_MMAP) {
            if (size < 16) return false;
            const struct multiboot_tag_mmap *mmap =
                (const struct multiboot_tag_mmap *)tag;
            uint32_t payload = size - 16;
            if (mmap->entry_size < sizeof(struct multiboot_tag_mmap_entry) ||
                payload % mmap->entry_size != 0)
                return false;

            uint32_t count = payload / mmap->entry_size;
            const uint8_t *entry = tag + 16;
            for (uint32_t i = 0; i < count; i++, entry += mmap->entry_size) {
                const struct multiboot_tag_mmap_entry *range =
                    (const struct multiboot_tag_mmap_entry *)entry;
                if (range->type != 1 || range->length == 0 ||
                    range->base_addr >= PMM_DIRECT_MAP_LIMIT)
                    continue;
                uint64_t end = range->base_addr + range->length;
                if (end < range->base_addr || end > PMM_DIRECT_MAP_LIMIT)
                    end = PMM_DIRECT_MAP_LIMIT;
                uint64_t start = (range->base_addr + PMM_PAGE_SIZE - 1) &
                                 ~(uint64_t)(PMM_PAGE_SIZE - 1);
                end &= ~(uint64_t)(PMM_PAGE_SIZE - 1);
                if (start < end && !pmm_add_range(start, end)) return false;
            }
            found_map = true;
        }
        if (type == MULTIBOOT_TAG_TYPE_END) break;
        if (size > UINT32_MAX - 7U) return false;
        uint32_t aligned_size = (size + 7U) & ~7U;
        if (aligned_size > total_size - offset) return false;
        offset += aligned_size;
    }

    if (!found_map || free_range_count == 0) return false;

    for (uint32_t i = 0; i < free_range_count; i++) {
        uint64_t pages = (free_ranges[i].end - free_ranges[i].start) / PMM_PAGE_SIZE;
        free_pages += pages;
    }
    total_pages = free_pages;
    return true;
}

static bool pmm_reserve_multiboot_tags(uintptr_t multiboot_info, uint32_t total_size) {
    const uint8_t *info = (const uint8_t *)multiboot_info;
    uint32_t offset = 8;
    while (total_size - offset >= 8) {
        const uint8_t *tag = info + offset;
        uint32_t type = *(const uint32_t *)tag;
        uint32_t size = *(const uint32_t *)(tag + 4);
        if (size < 8 || size > total_size - offset) return false;

        if (type == MULTIBOOT_TAG_TYPE_MODULE) {
            if (size < 16) return false;
            uint32_t start = *(const uint32_t *)(tag + 8);
            uint32_t end = *(const uint32_t *)(tag + 12);
            if (end < start || !pmm_reserve_range(start, end)) return false;
        }
        if (type == MULTIBOOT_TAG_TYPE_END) break;
        if (size > UINT32_MAX - 7U) return false;
        uint32_t aligned_size = (size + 7U) & ~7U;
        if (aligned_size > total_size - offset) return false;
        offset += aligned_size;
    }
    return true;
}

bool pmm_init(uintptr_t multiboot_info) {
    free_range_count = 0;
    allocated_range_count = 0;
    total_pages = 0;
    free_pages = 0;
    if (!multiboot_info || multiboot_info >= PMM_DIRECT_MAP_LIMIT) return false;

    const uint32_t *info = (const uint32_t *)multiboot_info;
    uint32_t total_size = info[0];
    if (total_size < 16 ||
        (uint64_t)multiboot_info + total_size > PMM_DIRECT_MAP_LIMIT ||
        !pmm_parse_multiboot(multiboot_info, total_size))
        return false;

    uint64_t kernel_start = (uintptr_t)__kernel_start;
    uint64_t kernel_end = (uintptr_t)__kernel_end;
    if (!pmm_reserve_range(0, 0x100000) ||
        !pmm_reserve_range(kernel_start, kernel_end) ||
        !pmm_reserve_range(multiboot_info, multiboot_info + total_size) ||
        !pmm_reserve_multiboot_tags(multiboot_info, total_size))
        return false;

    return total_pages != 0;
}

void *pmm_alloc_page(void) {
    uint64_t flags = pmm_disable_interrupts();
    if (free_range_count == 0) {
        pmm_restore_interrupts(flags);
        return 0;
    }
    uint64_t address = free_ranges[0].start;
    if (!pmm_range_can_add(allocated_ranges, allocated_range_count,
                           address, address + PMM_PAGE_SIZE)) {
        pmm_restore_interrupts(flags);
        return 0;
    }
    if (!pmm_insert_range(allocated_ranges, &allocated_range_count,
                          address, address + PMM_PAGE_SIZE)) {
        pmm_restore_interrupts(flags);
        return 0;
    }
    free_ranges[0].start += PMM_PAGE_SIZE;
    free_pages--;
    if (free_ranges[0].start == free_ranges[0].end) {
        for (uint32_t i = 0; i + 1 < free_range_count; i++)
            free_ranges[i] = free_ranges[i + 1];
        free_range_count--;
    }
    pmm_restore_interrupts(flags);

    uint8_t *page = (uint8_t *)(uintptr_t)address;
    for (uint32_t i = 0; i < PMM_PAGE_SIZE; i++) page[i] = 0;
    return page;
}

bool pmm_free_page(void *page) {
    uint64_t address = (uintptr_t)page;
    if (!page || (address & (PMM_PAGE_SIZE - 1)) != 0 ||
        address >= PMM_DIRECT_MAP_LIMIT)
        return false;

    uint64_t flags = pmm_disable_interrupts();
    uint64_t end = address + PMM_PAGE_SIZE;
    if (!pmm_range_contains(allocated_ranges, allocated_range_count, address, end) ||
        !pmm_range_can_add(free_ranges, free_range_count, address, end)) {
        pmm_restore_interrupts(flags);
        return false;
    }
    if (!pmm_remove_allocated_range(address, end)) {
        pmm_restore_interrupts(flags);
        return false;
    }

    if (!pmm_add_range(address, end)) {
        pmm_restore_interrupts(flags);
        return false;
    }
    free_pages++;
    pmm_restore_interrupts(flags);
    return true;
}

uint64_t pmm_total_bytes(void) {
    return total_pages * PMM_PAGE_SIZE;
}

uint64_t pmm_free_bytes(void) {
    uint64_t flags = pmm_disable_interrupts();
    uint64_t bytes = free_pages * PMM_PAGE_SIZE;
    pmm_restore_interrupts(flags);
    return bytes;
}

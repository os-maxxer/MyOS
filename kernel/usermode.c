#include <solis/usermode.h>
#include <solis/user_elf.h>
#include <solis/task.h>
#include <solis/console.h>
#include <solis/pmm.h>

extern void kernel_panic(const char *message) __attribute__((noreturn));

static void put_elf_u16(uint8_t *image, size_t offset, uint16_t value) {
    image[offset] = (uint8_t)value;
    image[offset + 1] = (uint8_t)(value >> 8);
}

static void put_elf_u32(uint8_t *image, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; i++)
        image[offset + i] = (uint8_t)(value >> (i * 8));
}

static void put_elf_u64(uint8_t *image, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8; i++)
        image[offset + i] = (uint8_t)(value >> (i * 8));
}

static void build_segment_boundary_elf(uint8_t *image) {
    for (size_t i = 0; i < 120; i++) image[i] = 0;
    image[0] = 0x7F;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 2;
    image[5] = 1;
    image[6] = 1;
    put_elf_u16(image, 16, 2);
    put_elf_u16(image, 18, 62);
    put_elf_u32(image, 20, 1);
    put_elf_u64(image, 24, VMM_USER_SPACE_END - PMM_PAGE_SIZE);
    put_elf_u64(image, 32, 64);
    put_elf_u16(image, 52, 64);
    put_elf_u16(image, 54, 56);
    put_elf_u16(image, 56, 1);
    put_elf_u32(image, 64, 1);
    put_elf_u32(image, 68, 5);
    put_elf_u64(image, 80, VMM_USER_SPACE_END - PMM_PAGE_SIZE);
    put_elf_u64(image, 104, PMM_PAGE_SIZE * 2);
}

static void assert_elf_rejected_without_leak(const uint8_t *image,
                                             size_t image_size,
                                             const char *failure_message) {
    struct vmm_address_space rejected_space = {0};
    uintptr_t rejected_entry = 0;
    uint64_t free_memory_before = pmm_free_bytes();
    if (user_elf_load(image, image_size, &rejected_space, &rejected_entry)) {
        if (!vmm_address_space_destroy(&rejected_space))
            kernel_panic("Unable to clean up accepted malformed ELF64 image");
        kernel_panic(failure_message);
    }
    if (pmm_free_bytes() != free_memory_before)
        kernel_panic("Rejected ELF64 image leaked physical memory");
}

void usermode_run_self_test(void) {
    console_write("[USER] starting task scheduler self-test\n");
    static const uint8_t malformed_elf[] = {0x7F, 'E', 'L', 'F'};
    assert_elf_rejected_without_leak(
        malformed_elf, sizeof(malformed_elf),
        "ELF64 loader accepted a truncated image");

    uint8_t malformed_segment_elf[120];
    build_segment_boundary_elf(malformed_segment_elf);
    put_elf_u64(malformed_segment_elf, 32, UINT64_MAX);
    assert_elf_rejected_without_leak(
        malformed_segment_elf, sizeof(malformed_segment_elf),
        "ELF64 loader accepted an out-of-range program table");

    build_segment_boundary_elf(malformed_segment_elf);
    assert_elf_rejected_without_leak(
        malformed_segment_elf, sizeof(malformed_segment_elf),
        "ELF64 loader accepted a segment outside user space");
    console_write("[USER] malformed ELF tables and segment bounds rejected without leaks\n");

    uint64_t free_memory_before = pmm_free_bytes();
    if (task_run_user_demo() != 0)
        kernel_panic("User task scheduler test returned an error");
    if (pmm_free_bytes() != free_memory_before)
        kernel_panic("User task cleanup leaked physical memory");
    console_write("[USER] isolated address spaces, round-robin yield, syscalls, and task cleanup passed\n");
}

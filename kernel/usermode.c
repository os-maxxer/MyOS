#include <solis/usermode.h>
#include <solis/user_elf.h>
#include <solis/vmm.h>
#include <solis/pmm.h>
#include <solis/console.h>

extern void kernel_panic(const char *message) __attribute__((noreturn));
extern const uint8_t _binary_build_user_test_elf_start[];
extern const uint8_t _binary_build_user_test_elf_end[];

#define USER_TEST_STACK_TOP (VMM_USER_SPACE_END - 0x2000)

void usermode_run_self_test(void) {
    static const uint8_t malformed_elf[] = {0x7F, 'E', 'L', 'F'};
    struct vmm_address_space rejected_space = {0};
    uintptr_t rejected_entry = 0;
    if (user_elf_load(malformed_elf, sizeof(malformed_elf),
                      &rejected_space, &rejected_entry))
        kernel_panic("ELF64 loader accepted a truncated image");

    struct vmm_address_space space = {0};
    uintptr_t entry = 0;
    size_t image_size = (size_t)(_binary_build_user_test_elf_end -
                                 _binary_build_user_test_elf_start);
    if (!user_elf_load(_binary_build_user_test_elf_start, image_size,
                       &space, &entry))
        kernel_panic("Unable to load user-mode ELF64 test program");

    uint64_t stack_flags = VMM_PAGE_USER | VMM_PAGE_WRITABLE;
    if (vmm_nx_enabled()) stack_flags |= VMM_PAGE_NO_EXECUTE;
    if (!vmm_address_space_map_alloc(
            &space, USER_TEST_STACK_TOP - PMM_PAGE_SIZE, stack_flags)) {
        vmm_activate_kernel();
        if (!vmm_address_space_destroy(&space))
            kernel_panic("Unable to clean up user-mode test address space");
        kernel_panic("Unable to allocate user-mode test stack");
    }

    if (!vmm_address_space_activate(&space))
        kernel_panic("Unable to activate user address space");
    int status = arch_enter_user(entry, USER_TEST_STACK_TOP);
    vmm_activate_kernel();
    if (!vmm_address_space_destroy(&space))
        kernel_panic("Unable to destroy user address space");
    if (status != 0)
        kernel_panic("User-mode ELF64 self-test returned an error");

    console_write("[USER] ELF64 loader, ring-3 entry, validated syscalls, and address-space cleanup passed\n");
}

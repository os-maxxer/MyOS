#ifndef SOLIS_USERMODE_H
#define SOLIS_USERMODE_H

#include <solis/types.h>

#define SOLIS_SYSCALL_VECTOR 0x80
#define SOLIS_SYSCALL_EXIT   0
#define SOLIS_SYSCALL_WRITE  1
#define SOLIS_SYSCALL_MAX_WRITE 256

/* int 0x80: syscall number in RAX; write takes buffer in RDI and length in RSI. */
int arch_enter_user(uintptr_t entry, uintptr_t user_stack);
void arch_exit_user(uint64_t status) __attribute__((noreturn));

void usermode_run_self_test(void);

#endif

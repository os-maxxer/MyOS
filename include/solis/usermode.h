#ifndef SOLIS_USERMODE_H
#define SOLIS_USERMODE_H

#include <solis/types.h>

#define SOLIS_SYSCALL_VECTOR 0x80
#define SOLIS_SYSCALL_EXIT   0
#define SOLIS_SYSCALL_WRITE  1
#define SOLIS_SYSCALL_YIELD  2
#define SOLIS_SYSCALL_GETPID 3
#define SOLIS_SYSCALL_SPAWN  4
#define SOLIS_SYSCALL_WAIT   5
#define SOLIS_SYSCALL_OPEN   6
#define SOLIS_SYSCALL_READ   7
#define SOLIS_SYSCALL_CLOSE  8
#define SOLIS_SYSCALL_MAX_WRITE 256
#define SOLIS_SYSCALL_MAX_PATH 64
#define SOLIS_SYSCALL_MAX_READ 256

/* int 0x80: syscall number in RAX; write takes buffer in RDI and length in RSI. */
/* open takes a path; read takes fd, buffer, and size; close takes an fd. */
int arch_enter_user(uintptr_t entry, uintptr_t user_stack);
void arch_exit_user(uint64_t status) __attribute__((noreturn));

void usermode_run_self_test(void);

#endif

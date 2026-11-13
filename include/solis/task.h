#ifndef SOLIS_TASK_H
#define SOLIS_TASK_H

#include <solis/interrupts.h>
#include <solis/types.h>

uint64_t task_current_pid(void);
struct registers *task_yield(struct registers *regs);
struct registers *task_preempt(struct registers *regs);
struct registers *task_exit(struct registers *regs, uint64_t status);
uint64_t task_spawn_from_user(uintptr_t user_path);
uint64_t task_open_file_from_user(uintptr_t user_path);
uint64_t task_read_file_from_user(uint64_t fd, uintptr_t user_buffer,
                                  uint64_t requested_size);
uint64_t task_close_file_from_user(uint64_t fd);
struct registers *task_wait(struct registers *regs, uint64_t child_pid);
int task_run_user_demo(void);

#endif

#include <solis/task.h>
#include <solis/pmm.h>
#include <solis/tss.h>
#include <solis/user_elf.h>
#include <solis/usermode.h>
#include <solis/vmm.h>
#include <solis/console.h>
#include <solis/vfs.h>
#include <stddef.h>

extern void kernel_panic(const char *message) __attribute__((noreturn));
extern int arch_enter_user(uintptr_t entry, uintptr_t user_stack);
extern void arch_exit_user(uint64_t status) __attribute__((noreturn));
extern const uint8_t _binary_build_user_test_elf_start[];
extern const uint8_t _binary_build_user_test_elf_end[];

#define TASK_COUNT 8
#define TASK_STACK_TOP (VMM_USER_SPACE_END - 0x2000)
#define TASK_EXECUTABLE_MAX_SIZE (128U * 1024U)
#define TASK_FILE_COUNT 4
#define TASK_FILE_HANDLE_BASE 3

_Static_assert(offsetof(struct registers, int_no) == 120,
               "interrupt assembly vector offset changed");
_Static_assert(sizeof(struct registers) == 176,
               "ring-3 interrupt frame layout changed");

enum task_state {
    TASK_UNUSED,
    TASK_RUNNABLE,
    TASK_BLOCKED,
    TASK_EXITED
};

struct task_file {
    int vfs_fd;
    uint32_t offset;
    bool open;
};

struct task {
    struct vmm_address_space address_space;
    struct registers *saved_frame;
    void *kernel_stack;
    uintptr_t entry;
    uint64_t pid;
    uint64_t parent_pid;
    uint64_t waiting_for_pid;
    uint64_t exit_status;
    bool wait_consumed;
    enum task_state state;
    struct task_file files[TASK_FILE_COUNT];
};

static struct task tasks[TASK_COUNT];
static uint8_t executable_image[TASK_EXECUTABLE_MAX_SIZE]
    __attribute__((section(".user_image_stage"), aligned(PMM_PAGE_SIZE)));
static int current_task = -1;
static bool scheduler_running;
static uintptr_t scheduler_kernel_rsp0;
static uint64_t next_pid = 1;

static void task_cleanup(struct task *task) {
    if (task->address_space.root) {
        vmm_activate_kernel();
        if (!vmm_address_space_destroy(&task->address_space))
            kernel_panic("Unable to destroy task address space");
    }
    if (task->kernel_stack && !pmm_free_page(task->kernel_stack))
        kernel_panic("Unable to free task kernel stack");
    *task = (struct task){0};
}

static bool task_create(struct task *task, uint64_t pid, uint64_t parent_pid,
                        const uint8_t *image, size_t image_size) {
    uintptr_t entry = 0;
    if (!user_elf_load(image, image_size, &task->address_space, &entry))
        return false;

    uint64_t user_stack_flags = VMM_PAGE_USER | VMM_PAGE_WRITABLE;
    if (vmm_nx_enabled()) user_stack_flags |= VMM_PAGE_NO_EXECUTE;
    if (!vmm_address_space_map_alloc(
            &task->address_space, TASK_STACK_TOP - PMM_PAGE_SIZE,
            user_stack_flags)) {
        task_cleanup(task);
        return false;
    }
    task->kernel_stack = pmm_alloc_page();
    if (!task->kernel_stack) {
        task_cleanup(task);
        return false;
    }

    uintptr_t stack_top = (uintptr_t)task->kernel_stack + PMM_PAGE_SIZE;
    struct registers *frame =
        (struct registers *)(stack_top - sizeof(struct registers));
    *frame = (struct registers){0};
    frame->rip = entry;
    frame->cs = 0x2B;
    frame->rflags = 0x2;
    frame->user_rsp = TASK_STACK_TOP;
    frame->user_ss = 0x33;

    task->entry = entry;
    task->pid = pid;
    task->parent_pid = parent_pid;
    task->saved_frame = frame;
    task->state = TASK_RUNNABLE;
    return true;
}

static int task_find_unused(void) {
    for (int i = 0; i < TASK_COUNT; i++)
        if (tasks[i].state == TASK_UNUSED) return i;
    return -1;
}

static bool task_copy_user_path(uintptr_t user_path, char *path) {
    if (!user_path || !path) return false;
    for (size_t i = 0; i < SOLIS_SYSCALL_MAX_PATH; i++) {
        uintptr_t source = user_path + i;
        if (source < user_path ||
            !vmm_copy_from_user(&path[i], source, sizeof(path[i])))
            return false;
        if (path[i] == '\0') return i > 0;
    }
    return false;
}

static int task_read_executable(const char *path, size_t *image_size) {
    int fd = vfs_open(path);
    if (fd < 0) return 0;
    int size = vfs_get_size(fd);
    if (size <= 0 || (uint32_t)size > sizeof(executable_image) ||
        vfs_read(fd, executable_image, (uint32_t)size) != size)
        return -1;
    *image_size = (size_t)size;
    return 1;
}

static int task_find_next(int after) {
    for (int offset = 1; offset <= TASK_COUNT; offset++) {
        int index = (after + offset) % TASK_COUNT;
        if (tasks[index].state == TASK_RUNNABLE) return index;
    }
    return -1;
}

static struct registers *task_switch_to(int index) {
    current_task = index;
    if (!vmm_address_space_activate(&tasks[index].address_space))
        kernel_panic("Unable to activate scheduled task address space");
    tss_set_rsp0((uintptr_t)tasks[index].kernel_stack + PMM_PAGE_SIZE);
    return tasks[index].saved_frame;
}

uint64_t task_current_pid(void) {
    if (!scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE)
        return 0;
    return tasks[current_task].pid;
}

uint64_t task_spawn_from_user(uintptr_t user_path) {
    if (!scheduler_running || current_task < 0 || user_path == 0)
        return UINT64_MAX;

    char path[SOLIS_SYSCALL_MAX_PATH];
    if (!task_copy_user_path(user_path, path)) return UINT64_MAX;

    size_t image_size;
    if (task_read_executable(path, &image_size) != 1) return UINT64_MAX;
    int slot = task_find_unused();
    if (slot < 0 || next_pid == 0) return UINT64_MAX;
    uint64_t pid = next_pid++;
    if (!task_create(&tasks[slot], pid, tasks[current_task].pid,
                     executable_image, image_size))
        return UINT64_MAX;
    return pid;
}

uint64_t task_open_file_from_user(uintptr_t user_path) {
    if (!scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE)
        return UINT64_MAX;

    char path[SOLIS_SYSCALL_MAX_PATH];
    if (!task_copy_user_path(user_path, path)) return UINT64_MAX;

    int slot = -1;
    for (int i = 0; i < TASK_FILE_COUNT; i++) {
        if (!tasks[current_task].files[i].open) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return UINT64_MAX;

    int vfs_fd = vfs_open(path);
    if (vfs_fd < 0 || vfs_get_size(vfs_fd) < 0) return UINT64_MAX;
    tasks[current_task].files[slot] =
        (struct task_file){.vfs_fd = vfs_fd, .offset = 0, .open = true};
    return TASK_FILE_HANDLE_BASE + (uint64_t)slot;
}

static struct task_file *task_file_from_handle(uint64_t fd) {
    if (fd < TASK_FILE_HANDLE_BASE ||
        fd >= TASK_FILE_HANDLE_BASE + TASK_FILE_COUNT ||
        current_task < 0 || current_task >= TASK_COUNT)
        return 0;
    struct task_file *file =
        &tasks[current_task].files[fd - TASK_FILE_HANDLE_BASE];
    return file->open ? file : 0;
}

uint64_t task_read_file_from_user(uint64_t fd, uintptr_t user_buffer,
                                  uint64_t requested_size) {
    if (!scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE ||
        requested_size > SOLIS_SYSCALL_MAX_READ)
        return UINT64_MAX;

    struct task_file *file = task_file_from_handle(fd);
    if (!file) return UINT64_MAX;
    int file_size = vfs_get_size(file->vfs_fd);
    if (file_size < 0) return UINT64_MAX;

    uint32_t read_size = (uint32_t)requested_size;
    if (file->offset >= (uint32_t)file_size) return 0;
    uint32_t remaining = (uint32_t)file_size - file->offset;
    if (read_size > remaining) read_size = remaining;
    uint8_t buffer[SOLIS_SYSCALL_MAX_READ];
    int bytes_read = vfs_read_at(file->vfs_fd, file->offset,
                                 buffer, read_size);
    if (bytes_read < 0 || (uint32_t)bytes_read != read_size ||
        !vmm_copy_to_user(user_buffer, buffer, read_size))
        return UINT64_MAX;
    file->offset += (uint32_t)bytes_read;
    return (uint64_t)bytes_read;
}

uint64_t task_close_file_from_user(uint64_t fd) {
    if (!scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE)
        return UINT64_MAX;
    struct task_file *file = task_file_from_handle(fd);
    if (!file) return UINT64_MAX;
    *file = (struct task_file){0};
    return 0;
}

static int task_find_pid(uint64_t pid) {
    for (int i = 0; i < TASK_COUNT; i++)
        if (tasks[i].state != TASK_UNUSED && tasks[i].pid == pid)
            return i;
    return -1;
}

static void task_reap(int index) {
    if (index < 0 || index >= TASK_COUNT || index == current_task ||
        tasks[index].state != TASK_EXITED || !tasks[index].wait_consumed)
        return;

    struct task *task = &tasks[index];
    struct vmm_address_space *current_space =
        current_task >= 0 ? &tasks[current_task].address_space : 0;
    vmm_activate_kernel();
    if (task->address_space.root &&
        !vmm_address_space_destroy(&task->address_space))
        kernel_panic("Unable to reap user task address space");
    if (task->kernel_stack && !pmm_free_page(task->kernel_stack))
        kernel_panic("Unable to reap user task kernel stack");
    *task = (struct task){0};
    if (current_space && !vmm_address_space_activate(current_space))
        kernel_panic("Unable to restore current task address space after reap");
}

static void task_reap_waited(void) {
    for (int i = 0; i < TASK_COUNT; i++) task_reap(i);
}

struct registers *task_wait(struct registers *regs, uint64_t child_pid) {
    if (!regs || !scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE ||
        child_pid == tasks[current_task].pid) {
        if (regs) regs->rax = UINT64_MAX;
        return regs;
    }

    int child = task_find_pid(child_pid);
    if (child < 0 || tasks[child].parent_pid != tasks[current_task].pid) {
        regs->rax = UINT64_MAX;
        return regs;
    }
    if (tasks[child].state == TASK_EXITED) {
        regs->rax = tasks[child].exit_status;
        tasks[child].wait_consumed = true;
        task_reap(child);
        return regs;
    }

    tasks[current_task].saved_frame = regs;
    tasks[current_task].waiting_for_pid = child_pid;
    tasks[current_task].state = TASK_BLOCKED;
    int next = task_find_next(current_task);
    if (next < 0) {
        tasks[current_task].state = TASK_RUNNABLE;
        tasks[current_task].waiting_for_pid = 0;
        regs->rax = UINT64_MAX;
        return regs;
    }
    return task_switch_to(next);
}

struct registers *task_yield(struct registers *regs) {
    task_reap_waited();
    if (!regs || !scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE) {
        if (regs) regs->rax = UINT64_MAX;
        return regs;
    }
    tasks[current_task].saved_frame = regs;
    regs->rax = 0;
    int next = task_find_next(current_task);
    return next < 0 ? regs : task_switch_to(next);
}

struct registers *task_preempt(struct registers *regs) {
    if (!regs || !scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE ||
        (regs->cs & 3) != 3)
        return regs;

    task_reap_waited();
    tasks[current_task].saved_frame = regs;
    int next = task_find_next(current_task);
    return next < 0 ? regs : task_switch_to(next);
}

struct registers *task_exit(struct registers *regs, uint64_t status) {
    if (!regs || !scheduler_running || current_task < 0 ||
        tasks[current_task].state != TASK_RUNNABLE) {
        if (regs) regs->rax = UINT64_MAX;
        return regs;
    }
    tasks[current_task].saved_frame = regs;
    tasks[current_task].exit_status = status;
    for (int i = 0; i < TASK_FILE_COUNT; i++)
        tasks[current_task].files[i] = (struct task_file){0};
    tasks[current_task].state = TASK_EXITED;
    uint64_t parent_pid = tasks[current_task].parent_pid;
    int parent = task_find_pid(parent_pid);
    if (parent >= 0 && tasks[parent].state == TASK_BLOCKED &&
        tasks[parent].waiting_for_pid == tasks[current_task].pid) {
        tasks[parent].saved_frame->rax = status;
        tasks[parent].waiting_for_pid = 0;
        tasks[parent].state = TASK_RUNNABLE;
        tasks[current_task].wait_consumed = true;
    }
    int next = task_find_next(current_task);
    if (next >= 0) return task_switch_to(next);

    uint64_t final_status = tasks[0].exit_status;
    scheduler_running = false;
    current_task = -1;
    vmm_activate_kernel();
    tss_set_rsp0(scheduler_kernel_rsp0);
    arch_exit_user(final_status);
}

int task_run_user_demo(void) {
    size_t image_size = (size_t)(_binary_build_user_test_elf_end -
                                 _binary_build_user_test_elf_start);
    const uint8_t *first_image = _binary_build_user_test_elf_start;
    size_t disk_image_size = 0;
    int disk_image_status =
        task_read_executable("/hello.elf", &disk_image_size);
    if (disk_image_status < 0)
        kernel_panic("Invalid /hello.elf file in SOLFS");
    if (disk_image_status > 0) {
        first_image = executable_image;
        image_size = disk_image_size;
        console_write("[TASK] loaded /hello.elf from SOLFS\n");
    } else {
        console_write("[TASK] /hello.elf not found; using embedded user test\n");
    }

    next_pid = 1;
    if (!task_create(&tasks[0], 1, 0, first_image, image_size))
        kernel_panic("Unable to create first user task");
    next_pid = 2;
    if (!task_create(&tasks[1], 2, 0, _binary_build_user_test_elf_start,
                     (size_t)(_binary_build_user_test_elf_end -
                              _binary_build_user_test_elf_start))) {
        task_cleanup(&tasks[0]);
        kernel_panic("Unable to create second user task");
    }
    next_pid = 3;

    scheduler_kernel_rsp0 = tss_get()->rsp0;
    scheduler_running = true;
    current_task = 0;
    if (!vmm_address_space_activate(&tasks[0].address_space))
        kernel_panic("Unable to activate initial task address space");
    tss_set_rsp0((uintptr_t)tasks[0].kernel_stack + PMM_PAGE_SIZE);
    int status = arch_enter_user(tasks[0].entry, TASK_STACK_TOP);

    scheduler_running = false;
    current_task = -1;
    vmm_activate_kernel();
    tss_set_rsp0(scheduler_kernel_rsp0);
    for (int i = 0; i < TASK_COUNT; i++) task_cleanup(&tasks[i]);
    return status;
}

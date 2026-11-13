#include <stdint.h>

#define SYSCALL_EXIT 0
#define SYSCALL_WRITE 1
#define SYSCALL_GETPID 3
#define SYSCALL_SPAWN 4
#define SYSCALL_WAIT 5
#define SYSCALL_OPEN 6
#define SYSCALL_READ 7
#define SYSCALL_CLOSE 8
#define MAX_TEST_ITERATIONS 20000000ULL

static inline uint64_t syscall0(uint64_t number) {
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "0"(number)
                     : "memory");
    return result;
}

static inline uint64_t syscall1(uint64_t number, uint64_t first) {
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "0"(number), "D"(first)
                     : "memory");
    return result;
}

static inline uint64_t syscall2(uint64_t number, uint64_t first,
                               uint64_t second) {
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "0"(number), "D"(first), "S"(second)
                     : "memory");
    return result;
}

static inline uint64_t syscall3(uint64_t number, uint64_t first,
                                uint64_t second, uint64_t third) {
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "0"(number), "D"(first), "S"(second), "d"(third)
                     : "memory");
    return result;
}

static const char task_one_started[] = "task 1 C user program started\n";
static const char task_two_started[] = "task 2 C user program started\n";
static const char task_three_started[] = "task 3 spawned from user mode\n";
static const char spawn_error[] = "user-mode process spawn failed\n";
static const char spawn_unavailable[] = "optional /hello.elf is unavailable\n";
static const char invalid_pointer_error[] = "invalid pointer was accepted\n";
static const char task_one_complete[] = "task 1 preempted loop completed\n";
static const char task_two_complete[] = "task 2 preempted loop completed\n";
static const char task_three_complete[] = "task 3 preempted loop completed\n";
static const char child_fault_isolated[] = "child fault isolated; wait status verified\n";
static const char wait_error[] = "child wait returned unexpected status\n";
static const char file_read_success[] = "per-process file descriptor read/close passed\n";
static const char file_isolation_error[] = "file descriptor leaked across tasks\n";
static const char file_open_error[] = "user file open failed\n";
static const char file_first_read_error[] = "user file first read failed\n";
static const char file_buffer_error[] = "user file buffer validation failed\n";
static const char file_offset_error[] = "user file offset handling failed\n";
static const char file_close_error[] = "user file close failed\n";

static int write_message(const char *message, uint64_t length) {
    return syscall2(SYSCALL_WRITE, (uintptr_t)message, length) == length;
}

int user_main(void) {
    uint64_t pid = syscall0(SYSCALL_GETPID);
    if (pid < 1 || pid > 3) return 1;
    int child_spawned = 0;

    const char *started = pid == 1 ? task_one_started
                                   : (pid == 2 ? task_two_started
                                               : task_three_started);
    uint64_t started_size = pid == 1 ? sizeof(task_one_started) - 1
                           : pid == 2 ? sizeof(task_two_started) - 1
                                      : sizeof(task_three_started) - 1;
    if (!write_message(started, started_size)) return 2;

    if (pid == 1) {
        static const char executable_path[] = "/hello.elf";
        uint64_t child_pid = syscall1(SYSCALL_SPAWN,
                                      (uintptr_t)executable_path);
        if (child_pid == UINT64_MAX) {
            if (!write_message(spawn_unavailable,
                               sizeof(spawn_unavailable) - 1))
                return 3;
        } else if (child_pid != 3) {
            write_message(spawn_error, sizeof(spawn_error) - 1);
            return 4;
        } else {
            child_spawned = 1;
        }
    }

    if (pid == 1 && child_spawned) {
        static const char repository_path[] = "/repo.json";
        uint64_t fd = syscall1(SYSCALL_OPEN, (uintptr_t)repository_path);
        char first_byte = 0;
        char second_byte = 0;
        char third_byte = 0;
        if (fd != 3) {
            write_message(file_open_error, sizeof(file_open_error) - 1);
            return 11;
        }
        if (syscall3(SYSCALL_READ, fd, (uintptr_t)&first_byte, 1) != 1 ||
            first_byte != '{') {
            write_message(file_first_read_error,
                          sizeof(file_first_read_error) - 1);
            return 11;
        }
        if (syscall3(SYSCALL_READ, fd, (uintptr_t)repository_path, 1) !=
                UINT64_MAX ||
            syscall3(SYSCALL_READ, fd, 0x7FFFFFFFFFFF, 1) != UINT64_MAX ||
            syscall3(SYSCALL_READ, fd, (uintptr_t)&second_byte, 257) !=
                UINT64_MAX) {
            write_message(file_buffer_error, sizeof(file_buffer_error) - 1);
            return 11;
        }
        if (syscall3(SYSCALL_READ, fd, (uintptr_t)&second_byte, 1) != 1 ||
            second_byte != '\r' ||
            syscall3(SYSCALL_READ, fd, (uintptr_t)&third_byte, 1) != 1 ||
            third_byte != '\n') {
            write_message(file_offset_error, sizeof(file_offset_error) - 1);
            return 11;
        }
        uint8_t chunk[256];
        if (syscall3(SYSCALL_READ, fd, (uintptr_t)chunk, sizeof(chunk)) != 256 ||
            syscall3(SYSCALL_READ, fd, (uintptr_t)chunk, sizeof(chunk)) != 256 ||
            syscall3(SYSCALL_READ, fd, (uintptr_t)chunk, sizeof(chunk)) != 129 ||
            syscall3(SYSCALL_READ, fd, (uintptr_t)chunk, sizeof(chunk)) != 0) {
            write_message(file_offset_error, sizeof(file_offset_error) - 1);
            return 11;
        }
        uint64_t fd4 = syscall1(SYSCALL_OPEN, (uintptr_t)repository_path);
        uint64_t fd5 = syscall1(SYSCALL_OPEN, (uintptr_t)repository_path);
        uint64_t fd6 = syscall1(SYSCALL_OPEN, (uintptr_t)repository_path);
        if (fd4 != 4 || fd5 != 5 || fd6 != 6 ||
            syscall1(SYSCALL_OPEN, (uintptr_t)repository_path) != UINT64_MAX ||
            syscall1(SYSCALL_CLOSE, fd4) != 0 ||
            syscall1(SYSCALL_OPEN, (uintptr_t)repository_path) != fd4 ||
            syscall1(SYSCALL_CLOSE, fd4) != 0 ||
            syscall1(SYSCALL_CLOSE, fd5) != 0 ||
            syscall1(SYSCALL_CLOSE, fd6) != 0) {
            write_message(file_close_error, sizeof(file_close_error) - 1);
            return 11;
        }
        if (syscall1(SYSCALL_CLOSE, fd) != 0 ||
            syscall3(SYSCALL_READ, fd, (uintptr_t)&second_byte, 1) !=
                UINT64_MAX) {
            write_message(file_close_error, sizeof(file_close_error) - 1);
            return 11;
        }
        if (!write_message(file_read_success, sizeof(file_read_success) - 1))
            return 12;
    }
    if (pid == 3) {
        char byte = 0;
        if (syscall3(SYSCALL_READ, 3, (uintptr_t)&byte, 1) != UINT64_MAX) {
            write_message(file_isolation_error,
                          sizeof(file_isolation_error) - 1);
            return 13;
        }
    }

    if (syscall0(UINT64_MAX) != UINT64_MAX) return 5;

    if (syscall2(SYSCALL_WRITE, 0x100000, 1) != UINT64_MAX) {
        write_message(invalid_pointer_error, sizeof(invalid_pointer_error) - 1);
        return 6;
    }

    for (volatile uint64_t i = 0; i < MAX_TEST_ITERATIONS; i++) {
        __asm__ volatile("" : : : "memory");
    }

    const char *complete = pid == 1 ? task_one_complete
                                    : (pid == 2 ? task_two_complete
                                                : task_three_complete);
    uint64_t complete_size = pid == 1 ? sizeof(task_one_complete) - 1
                           : pid == 2 ? sizeof(task_two_complete) - 1
                                      : sizeof(task_three_complete) - 1;
    if (!write_message(complete, complete_size)) return 7;

    if (pid == 3) {
        __asm__ volatile("ud2");
        return 8;
    }
    if (pid == 1 && child_spawned) {
        uint64_t status = syscall1(SYSCALL_WAIT, 3);
        if (status != 134) {
            write_message(wait_error, sizeof(wait_error) - 1);
            return 9;
        }
        if (!write_message(child_fault_isolated,
                           sizeof(child_fault_isolated) - 1))
            return 10;
    }
    return (int)syscall1(SYSCALL_EXIT, 0);
}

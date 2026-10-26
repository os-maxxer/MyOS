.intel_syntax noprefix
.section .text
.global _start
.type _start, @function
_start:
    /* A kernel-space pointer must be rejected by the write syscall. */
    mov eax, 1
    mov rdi, 0x100000
    mov esi, 1
    int 0x80
    cmp rax, -1
    jne test_failed

    lea rdi, [rip + success_message]
    mov esi, success_message_end - success_message
    mov eax, 1
    int 0x80
    cmp rax, success_message_end - success_message
    jne test_failed

    xor eax, eax
    xor edi, edi
    int 0x80

test_failed:
    xor eax, eax
    mov edi, 1
    int 0x80
    ud2

.section .rodata
success_message:
    .ascii "ELF64 ring-3 load and invalid-pointer checks passed\n"
success_message_end:

.section .note.GNU-stack,"",@progbits

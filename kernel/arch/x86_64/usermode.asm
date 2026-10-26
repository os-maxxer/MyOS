.intel_syntax noprefix
.text
.code64

.global arch_enter_user
arch_enter_user:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov qword ptr [rip + user_kernel_rsp], rsp
    pushfq
    pop rax
    and rax, 0xffffffffffffcfff
    mov qword ptr [rip + user_kernel_rflags], rax
    mov ax, 0x33
    mov ds, ax
    mov es, ax
    push 0x33
    push rsi
    push qword ptr [rip + user_kernel_rflags]
    push 0x2b
    push rdi
    iretq

.global arch_exit_user
arch_exit_user:
    cli
    mov rax, rdi
    mov rsp, qword ptr [rip + user_kernel_rsp]
    push qword ptr [rip + user_kernel_rflags]
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

.bss
.balign 8
user_kernel_rsp:
    .quad 0
user_kernel_rflags:
    .quad 0

.section .note.GNU-stack,"",@progbits

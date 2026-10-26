.intel_syntax noprefix
.text
.code64

.extern isr_common_handler
.extern irq_common_handler

.macro isr_noerrcode num
.global isr\num
isr\num:
    cli
    push 0
    push \num
    jmp isr_common_stub
.endm

.macro isr_errcode num
.global isr\num
isr\num:
    cli
    push \num
    jmp isr_common_stub
.endm

.macro irq num vector
.global irq\num
irq\num:
    cli
    push 0
    push \vector
    jmp irq_common_stub
.endm

isr_noerrcode 0
isr_noerrcode 1
isr_noerrcode 2
isr_noerrcode 3
isr_noerrcode 4
isr_noerrcode 5
isr_noerrcode 6
isr_noerrcode 7
isr_errcode 8
isr_noerrcode 9
isr_errcode 10
isr_errcode 11
isr_errcode 12
isr_errcode 13
isr_errcode 14
isr_noerrcode 15
isr_noerrcode 16
isr_errcode 17
isr_noerrcode 18
isr_noerrcode 19
isr_noerrcode 20
isr_errcode 21
isr_noerrcode 22
isr_noerrcode 23
isr_noerrcode 24
isr_noerrcode 25
isr_noerrcode 26
isr_noerrcode 27
isr_noerrcode 28
isr_errcode 29
isr_errcode 30
isr_noerrcode 31
isr_noerrcode 128

irq 0, 32
irq 1, 33
irq 2, 34
irq 3, 35
irq 4, 36
irq 5, 37
irq 6, 38
irq 7, 39
irq 8, 40
irq 9, 41
irq 10, 42
irq 11, 43
irq 12, 44
irq 13, 45
irq 14, 46
irq 15, 47

.macro save_registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
.endm

.macro restore_registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
.endm

isr_common_stub:
    save_registers
    mov r12, rsp
    mov rdi, rsp
    and rsp, -16
    call isr_common_handler
    mov rsp, r12
    restore_registers
    add rsp, 16
    iretq

irq_common_stub:
    save_registers
    mov r12, rsp
    mov rdi, rsp
    and rsp, -16
    call irq_common_handler
    mov rsp, r12

    cmp qword ptr [r12 + 120], 40
    jb 1f
    mov al, 0x20
    out 0xA0, al
1:
    mov al, 0x20
    out 0x20, al

    restore_registers
    add rsp, 16
    iretq

.global load_gdt
load_gdt:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

.global load_idt
load_idt:
    lidt [rdi]
    ret

.global load_tss
load_tss:
    mov ax, 0x18
    ltr ax
    ret

.global enable_interrupts
enable_interrupts:
    sti
    ret

.global disable_interrupts
disable_interrupts:
    cli
    ret

.section .note.GNU-stack,"",@progbits

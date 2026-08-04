.intel_syntax noprefix
.text

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
isr_noerrcode 21
isr_noerrcode 22
isr_noerrcode 23
isr_noerrcode 24
isr_noerrcode 25
isr_noerrcode 26
isr_noerrcode 27
isr_noerrcode 28
isr_noerrcode 29
isr_noerrcode 30
isr_noerrcode 31

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

isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp
    push eax
    call isr_common_handler
    pop eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

irq_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, esp
    push eax
    call irq_common_handler
    pop eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    push eax
    mov eax, [esp+4]
    cmp eax, 40
    jb .done
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    jmp .skip
.done:
    mov al, 0x20
    out 0x20, al
.skip:
    pop eax
    add esp, 8
    iret

.global load_gdt
load_gdt:
    mov eax, [esp+4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

.global load_idt
load_idt:
    mov eax, [esp+4]
    lidt [eax]
    ret

.global enable_interrupts
enable_interrupts:
    sti
    ret

.global disable_interrupts
disable_interrupts:
    cli
    ret

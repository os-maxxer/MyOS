/*
 * Boot entry point for MyOS.
 * This assembly file provides the Multiboot2-compliant entry and sets up the initial stack.
 */

.section .multiboot
.align 8
multiboot_header:
    .long 0xE85250D6
    .long 0
    .long multiboot_header_end - multiboot_header
    .long -(0xE85250D6 + 0 + (multiboot_header_end - multiboot_header))
    .align 8
    .long 5
    .long 8
    .align 8
    .long 0
    .long 8
multiboot_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli
    mov $stack_top, %esp
    push %ebx
    push %eax
    call kernel_main
    cli
1:
    hlt
    jmp 1b

.size _start, . - _start

/* Multiboot2 enters in 32-bit protected mode; this stub enables long mode. */

.section .multiboot
.align 8
multiboot_header:
    .long 0xE85250D6
    .long 0
    .long multiboot_header_end - multiboot_header
    .long -(0xE85250D6 + 0 + (multiboot_header_end - multiboot_header))
    .align 8
    .long (5 | (1 << 16))
    .long 20
    .long 1024
    .long 768
    .long 32
    .align 8
    .long 0
    .long 8
multiboot_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

.align 4096
.global boot_pml4
boot_pml4:
    .skip 4096
.align 4096
boot_pdpt:
    .skip 4096
.align 4096
boot_page_directories:
    .skip 512 * 4096

.align 4
boot_magic:
    .skip 4
boot_info:
    .skip 4

.section .rodata
.align 8
boot_unsupported_message:
    .asciz "Solis requires x86-64 long mode and PAE.\r\n"
.align 8
boot_gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF
boot_gdt_end:
boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt

.section .text
.global _start
.type _start, @function
.code32
_start:
    cli
    mov %eax, boot_magic
    mov %ebx, boot_info

    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb boot_unsupported
    mov $0x80000001, %eax
    cpuid
    test $0x20000000, %edx
    jz boot_unsupported
    mov $1, %eax
    cpuid
    test $0x40, %edx
    jz boot_unsupported

    /* A single PML4 entry maps the low 512 GiB with 2 MiB pages. */
    mov $boot_pml4, %edi
    mov $boot_pdpt, %eax
    or $3, %eax
    mov %eax, (%edi)

    mov $boot_pdpt, %esi
    mov $boot_page_directories, %edi
    mov $512, %ecx
1:
    mov %edi, %eax
    or $3, %eax
    mov %eax, (%esi)
    movl $0, 4(%esi)
    add $4096, %edi
    add $8, %esi
    loop 1b

    mov $boot_page_directories, %edi
    mov $0x83, %eax
    xor %edx, %edx
    mov $262144, %ecx
2:
    mov %eax, (%edi)
    mov %edx, 4(%edi)
    add $8, %edi
    add $0x200000, %eax
    adc $0, %edx
    loop 2b

    lgdt boot_gdt_ptr
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    mov $boot_pml4, %eax
    mov %eax, %cr3

    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    cld
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs

    mov $stack_top, %rsp
    mov boot_magic(%rip), %edi
    mov boot_info(%rip), %esi
    call kernel_main
    cli
3:
    hlt
    jmp 3b

boot_unsupported:
    cld
    mov $boot_unsupported_message, %esi
    mov $0xE9, %dx
4:
    lodsb
    test %al, %al
    jz 5f
    outb %al, %dx
    jmp 4b
5:
    cli
6:
    hlt
    jmp 6b

.size _start, . - _start

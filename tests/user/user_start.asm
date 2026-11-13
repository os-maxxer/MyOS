.intel_syntax noprefix
.section .text
.global _start
.type _start, @function
_start:
    call user_main
    mov edi, eax
    xor eax, eax
    int 0x80
    ud2

.section .note.GNU-stack,"",@progbits

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
}

SECTIONS {
    . = 0x0000010000001000;
    .text : { *(.text*) } :text
    . = ALIGN(0x1000);
    .rodata : { *(.rodata*) } :rodata
}

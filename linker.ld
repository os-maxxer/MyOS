ENTRY(_start)

SECTIONS {
    . = 1M;

    __text_start = .;
    .text ALIGN(4K) : {
        KEEP(*(.multiboot))
        *(.text*)
    }
    __text_end = .;

    __rodata_start = .;
    .rodata ALIGN(4K) : {
        *(.rodata*)
    }
    __rodata_end = .;

    .spx_apps ALIGN(4K) : {
        *(.spx_apps*)
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    }
}

ENTRY(_start)

SECTIONS {
    . = 1M;

    .text ALIGN(4K) : {
        KEEP(*(.multiboot))
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

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

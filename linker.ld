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

    /* The SPX slots occupy 16-36 MiB. Keep the GUI staging buffer beyond them. */
    .frame_stage 0x02400000 (NOLOAD) : {
        *(.frame_stage)
    }
}

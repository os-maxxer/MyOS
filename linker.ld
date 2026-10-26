OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS {
    . = 1M;

    __kernel_start = .;
    .text ALIGN(4K) : {
        __text_start = .;
        KEEP(*(.multiboot))
        *(.text*)
        . = ALIGN(4K);
        __text_end = .;
    }

    .rodata ALIGN(4K) : {
        __rodata_start = .;
        *(.rodata*)
        . = ALIGN(4K);
        __rodata_end = .;
    }

    .spx_apps ALIGN(4K) : {
        __spx_apps_start = .;
        *(.spx_apps*)
        . = ALIGN(4K);
        __spx_apps_end = .;
    }

    .data ALIGN(4K) : {
        __data_start = .;
        *(.data*)
        . = ALIGN(4K);
        __data_end = .;
    }

    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        . = ALIGN(4K);
        __bss_end = .;
    }

    /* The SPX slots occupy 16-36 MiB. Keep the GUI staging buffer beyond them. */
    .frame_stage 0x02400000 (NOLOAD) : {
        __frame_stage_start = .;
        *(.frame_stage)
        __frame_stage_end = .;
    }
    __kernel_end = .;
}

SECTIONS
{
    PROVIDE(BASE = 0x01000000);
    . = BASE;
    .text : {
        *(.spx_exports)
        *(.text*)
        *(.rodata*)
        *(.data*)
    }
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}

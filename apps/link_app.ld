SECTIONS
{
    PROVIDE(BASE = 0x01000000);
    . = BASE;
    .text : {
        *(.npx_exports)
        *(.text*)
        *(.rodata*)
        *(.data*)
    }
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}

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

    /* Fourteen SPX slots occupy 16-58 MiB (0x01000000-0x03A00000). The user
     * image staging buffer sits immediately above them and the scaled frame
     * buffer lives above the desktop cache, so loading the last app cannot
     * overwrite either stage. Both stages are mapped read/write and never
     * executable, so app code must never be loaded into them. */
    .user_image_stage 0x03A00000 (NOLOAD) : {
        __user_image_stage_start = .;
        *(.user_image_stage)
        __user_image_stage_end = .;
    }

    __kernel_end = .;

    /* Large desktop caches are deliberately outside the kernel BSS. This
     * keeps the linked kernel below the 16 MiB SPX slot base even with the
     * embedded Doom WAD, and lets the physical allocator reserve them as one
     * fixed, contiguous region. */
    .gui_cache 0x08000000 (NOLOAD) : {
        __gui_cache_start = .;
        *(.gui_cache)
        . = ALIGN(4K);
        __gui_cache_end = .;
    }

    .frame_stage 0x0A000000 (NOLOAD) : {
        __frame_stage_start = .;
        *(.frame_stage)
        __frame_stage_end = .;
    }

    ASSERT(__bss_end <= 0x01000000, "Kernel BSS overlaps SPX app slots")
}

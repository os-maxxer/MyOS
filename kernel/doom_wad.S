/* The shareware WAD is read-only data shared by the Doom app through the
 * syscall table. Keeping it out of the SPX slot avoids a 4 MiB copy twice. */
.section .rodata
.align 16
.global doom_wad_start
.global doom_wad_end
doom_wad_start:
.incbin "third_party/doom/doom1.wad"
doom_wad_end:

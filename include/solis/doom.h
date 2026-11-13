#ifndef SOLIS_DOOM_H
#define SOLIS_DOOM_H

#include <solis/types.h>

/* The shareware Doom zone and its loader need more room than one SPX slot.
 * This fixed region is identity mapped by the boot page tables and reserved
 * from the physical allocator before any driver starts using pages. */
#define SOLIS_DOOM_HEAP_BASE 0x04000000ULL
#define SOLIS_DOOM_HEAP_SIZE (16ULL * 1024 * 1024)
#define SOLIS_DOOM_HEAP_END  (SOLIS_DOOM_HEAP_BASE + SOLIS_DOOM_HEAP_SIZE)

/* Doom renders at its native 640x400 resolution and the desktop scales it to
 * the client rectangle with the kernel fast blitter. */
#define SOLIS_DOOM_FRAME_W 640
#define SOLIS_DOOM_FRAME_H 400

#endif

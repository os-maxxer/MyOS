#ifndef SOLIS_JIT_H
#define SOLIS_JIT_H

#include <solis/types.h>

/*
 * Native user programs produced by the in-system C compiler are executed in
 * a fixed kernel virtual window.  The compiler emits absolute addresses for
 * this window, so the loader must use the same constants.
 *
 * Code is mapped read-only/executable after it has been copied in.  Data,
 * string literals and globals are mapped read-write/no-execute in a separate
 * window.
 */
#define SOLIS_JIT_CODE_BASE ((uintptr_t)0xFFFFA00000000000ULL)
#define SOLIS_JIT_DATA_BASE ((uintptr_t)0xFFFFA00000080000ULL)
#define SOLIS_JIT_MAX_CODE  (64U * 1024U)
#define SOLIS_JIT_MAX_DATA  (64U * 1024U)

#define SOLIS_SCX_MAGIC   0x31584353U /* "SCX1" */
#define SOLIS_SCX_VERSION 1U

struct scx_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t entry;
    uint32_t reserved;
} __attribute__((packed));

/* Execute one already-linked SCX program image. Returns main's exit status,
 * or -1 when the image is empty, oversized, or cannot be mapped. */
int jit_execute(const uint8_t *code, uint32_t code_size,
                const uint8_t *data, uint32_t data_size);

#endif

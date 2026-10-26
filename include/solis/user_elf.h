#ifndef SOLIS_USER_ELF_H
#define SOLIS_USER_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <solis/types.h>
#include <solis/vmm.h>

/* Initialize space to {0}; on success it owns the loaded address space. */
bool user_elf_load(const uint8_t *image, size_t image_size,
                   struct vmm_address_space *space, uintptr_t *entry);

#endif

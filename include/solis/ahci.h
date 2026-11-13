#ifndef SOLIS_AHCI_H
#define SOLIS_AHCI_H

#include <solis/types.h>
#include <stdbool.h>

bool ahci_init(void);
bool ahci_present(void);
int ahci_read_sector(uint64_t lba, uint8_t *buf);
int ahci_write_sector(uint64_t lba, const uint8_t *buf);
uint64_t ahci_get_sector_count(void);
const char *ahci_get_serial(void);
const char *ahci_get_model(void);

#endif

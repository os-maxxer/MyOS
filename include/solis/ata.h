#ifndef SOLIS_ATA_H
#define SOLIS_ATA_H

#include <solis/types.h>
#include <stdbool.h>

#define ATA_SECTOR_SIZE 512

bool ata_init(void);
int  ata_read_sector(uint32_t lba, uint8_t *buf);
int  ata_write_sector(uint32_t lba, const uint8_t *buf);
bool ata_present(void);
const char* ata_get_serial(void);

#endif

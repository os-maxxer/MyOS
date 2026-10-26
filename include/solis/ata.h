#ifndef SOLIS_ATA_H
#define SOLIS_ATA_H

#include <solis/types.h>
#include <stdbool.h>

#define ATA_SECTOR_SIZE 512

bool ata_init(void);
int  ata_read_sector(uint64_t lba, uint8_t *buf);
int  ata_write_sector(uint64_t lba, const uint8_t *buf);
bool ata_present(void);
uint64_t ata_get_sector_count(void);
bool ata_get_solfs_region(uint64_t *start_lba, uint64_t *sector_count);
const char* ata_get_serial(void);
const char* ata_get_model(void);

#endif

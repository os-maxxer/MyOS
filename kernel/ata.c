#include <solis/ata.h>
#include <solis/ports.h>
#include <solis/console.h>

#define ATA_DATA     0x1F0
#define ATA_ERROR    0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA_LO   0x1F3
#define ATA_LBA_MID  0x1F4
#define ATA_LBA_HI   0x1F5
#define ATA_DRIVE    0x1F6
#define ATA_COMMAND  0x1F7
#define ATA_STATUS   0x1F7

#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_IDENT 0xEC
#define ATA_CMD_READ_EXT  0x24
#define ATA_CMD_WRITE_EXT 0x34

#define STATUS_BSY  0x80
#define STATUS_DRDY 0x40
#define STATUS_DRQ  0x08
#define STATUS_ERR  0x01

#define GPT_HEADER_LBA 1
#define GPT_MAX_ENTRIES 128
#define GPT_ENTRY_SIZE 128
#define GPT_SIGNATURE "EFI PART"

static bool disk_present = false;
static bool disk_lba48 = false;
static uint64_t disk_sector_count = 0;
static char disk_serial[21];
static char disk_model[41];

static const uint8_t solis_gpt_type_guid[16] = {
    0x13, 0x9E, 0x5D, 0x7A, 0xB4, 0x45, 0x2A, 0x4C,
    0x8C, 0x74, 0x53, 0x4F, 0x4C, 0x49, 0x53, 0x46
};

static int ata_wait(bool wait_for_drq) {
    int timeout = 1000000;
    while (--timeout) {
        uint8_t status = inb(ATA_STATUS);
        if (status & STATUS_ERR) return -1;
        if (!(status & STATUS_BSY)) {
            if (!wait_for_drq || (status & STATUS_DRQ)) return 0;
        }
    }
    return -1;
}

static uint32_t gpt_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1));
    }
    return crc;
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0]
         | ((uint32_t)data[1] << 8)
         | ((uint32_t)data[2] << 16)
         | ((uint32_t)data[3] << 24);
}

static uint64_t read_le64(const uint8_t *data) {
    return (uint64_t)read_le32(data) | ((uint64_t)read_le32(data + 4) << 32);
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (left[i] != right[i])
            return false;
    }
    return true;
}

static int gpt_find_solis_region(uint64_t *start_lba, uint64_t *sector_count) {
    uint8_t sector[ATA_SECTOR_SIZE];
    uint8_t header[ATA_SECTOR_SIZE];

    if (ata_read_sector(GPT_HEADER_LBA, sector) != 0)
        return -1;
    if (!bytes_equal(sector, (const uint8_t *)GPT_SIGNATURE, 8))
        return 2;

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++)
        header[i] = sector[i];

    uint32_t header_size = read_le32(header + 12);
    uint32_t expected_header_crc = read_le32(header + 16);
    if (header_size < 92 || header_size > ATA_SECTOR_SIZE)
        return -1;

    header[16] = 0;
    header[17] = 0;
    header[18] = 0;
    header[19] = 0;
    uint32_t header_crc = gpt_crc32_update(0xFFFFFFFFU, header, header_size) ^ 0xFFFFFFFFU;
    if (header_crc != expected_header_crc)
        return -1;

    uint64_t entries_lba = read_le64(sector + 72);
    uint32_t entry_count = read_le32(sector + 80);
    uint32_t entry_size = read_le32(sector + 84);
    uint32_t expected_entries_crc = read_le32(sector + 88);
    if (!entry_count || entry_count > GPT_MAX_ENTRIES || entry_size != GPT_ENTRY_SIZE)
        return -1;

    uint64_t entries_bytes = (uint64_t)entry_count * entry_size;
    uint64_t entries_sectors = (entries_bytes + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (entries_lba >= disk_sector_count || entries_sectors > disk_sector_count - entries_lba)
        return -1;

    uint32_t entries_crc = 0xFFFFFFFFU;
    bool found_solis = false;
    uint64_t found_start = 0;
    uint64_t found_count = 0;
    for (uint64_t i = 0; i < entries_sectors; i++) {
        if (ata_read_sector(entries_lba + i, sector) != 0)
            return -1;
        uint64_t offset = i * ATA_SECTOR_SIZE;
        uint32_t remaining = (uint32_t)(entries_bytes - offset);
        uint32_t bytes = remaining < ATA_SECTOR_SIZE ? remaining : ATA_SECTOR_SIZE;
        entries_crc = gpt_crc32_update(entries_crc, sector, bytes);

        for (uint32_t entry_offset = 0; entry_offset + GPT_ENTRY_SIZE <= bytes;
             entry_offset += GPT_ENTRY_SIZE) {
            const uint8_t *entry = sector + entry_offset;
            if (!bytes_equal(entry, solis_gpt_type_guid, sizeof(solis_gpt_type_guid)))
                continue;

            uint64_t first_lba = read_le64(entry + 32);
            uint64_t last_lba = read_le64(entry + 40);
            if (first_lba > last_lba || last_lba >= disk_sector_count)
                return -1;
            if (found_solis)
                return -1;
            found_solis = true;
            found_start = first_lba;
            found_count = last_lba - first_lba + 1;
        }
    }

    if ((entries_crc ^ 0xFFFFFFFFU) != expected_entries_crc)
        return -1;
    if (!found_solis)
        return 1;

    *start_lba = found_start;
    *sector_count = found_count;
    return 0;
}

bool ata_init(void) {
    disk_present = false;
    disk_lba48 = false;
    disk_sector_count = 0;

    outb(ATA_DRIVE, 0xE0);
    if (ata_wait(false) != 0)
        return false;

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);

    outb(ATA_COMMAND, ATA_CMD_IDENT);
    if (inb(ATA_STATUS) == 0 || inb(ATA_STATUS) == 0xFF) {
        console_write("ATA: no drive detected.\n");
        return false;
    }

    if (ata_wait(true) != 0) {
        console_write("ATA: drive timeout.\n");
        return false;
    }

    uint16_t id_buf[256];
    for (int i = 0; i < 256; i++)
        id_buf[i] = inw(ATA_DATA);

    disk_lba48 = (id_buf[83] & (1U << 10)) != 0;
    if (disk_lba48) {
        disk_sector_count = (uint64_t)id_buf[100]
                          | ((uint64_t)id_buf[101] << 16)
                          | ((uint64_t)id_buf[102] << 32)
                          | ((uint64_t)id_buf[103] << 48);
    } else if (id_buf[49] & (1U << 9)) {
        disk_sector_count = (uint64_t)id_buf[60]
                          | ((uint64_t)id_buf[61] << 16);
    }
    if (!disk_sector_count) {
        console_write("ATA: drive reports no LBA capacity.\n");
        return false;
    }

    for (int i = 0; i < 20; i++)
        disk_serial[i] = ' ';
    disk_serial[20] = '\0';
    for (int i = 0; i < 10; i++) {
        disk_serial[i * 2]     = id_buf[10 + i] & 0xFF;
        disk_serial[i * 2 + 1] = (id_buf[10 + i] >> 8) & 0xFF;
    }
    for (int i = 19; i >= 0; i--) {
        if (disk_serial[i] == ' ') disk_serial[i] = '\0';
        else break;
    }

    for (int i = 0; i < 20; i++) {
        uint16_t word = id_buf[27 + i];
        disk_model[i * 2] = (char)(word >> 8);
        disk_model[i * 2 + 1] = (char)word;
    }
    disk_model[40] = '\0';
    for (int i = 39; i >= 0; i--) {
        if (disk_model[i] == ' ' || disk_model[i] == '\0') disk_model[i] = '\0';
        else break;
    }

    disk_present = true;
    return true;
}

int ata_read_sector(uint64_t lba, uint8_t *buf) {
    if (!disk_present || !buf || lba >= disk_sector_count)
        return -1;

    if (ata_wait(false) != 0) return -1;

    outb(ATA_DRIVE, 0xE0);
    if (disk_lba48) {
        outb(ATA_SECCOUNT, 0);
        outb(ATA_LBA_LO, (lba >> 24) & 0xFF);
        outb(ATA_LBA_MID, (lba >> 32) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 40) & 0xFF);
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA_LO, lba & 0xFF);
        outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
        outb(ATA_COMMAND, ATA_CMD_READ_EXT);
    } else {
        if (lba > 0x0FFFFFFFU)
            return -1;
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA_LO, lba & 0xFF);
        outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
        outb(ATA_COMMAND, ATA_CMD_READ);
    }

    if (ata_wait(true) != 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t val = inw(ATA_DATA);
        buf[i * 2] = val & 0xFF;
        buf[i * 2 + 1] = (val >> 8) & 0xFF;
    }

    return 0;
}

int ata_write_sector(uint64_t lba, const uint8_t *buf) {
    if (!disk_present || !buf || lba >= disk_sector_count)
        return -1;

    if (ata_wait(false) != 0) return -1;

    outb(ATA_DRIVE, 0xE0);
    if (disk_lba48) {
        outb(ATA_SECCOUNT, 0);
        outb(ATA_LBA_LO, (lba >> 24) & 0xFF);
        outb(ATA_LBA_MID, (lba >> 32) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 40) & 0xFF);
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA_LO, lba & 0xFF);
        outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
        outb(ATA_COMMAND, ATA_CMD_WRITE_EXT);
    } else {
        if (lba > 0x0FFFFFFFU)
            return -1;
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA_LO, lba & 0xFF);
        outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
        outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
        outb(ATA_COMMAND, ATA_CMD_WRITE);
    }

    if (ata_wait(true) != 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t val = buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(ATA_DATA, val);
    }

    if (ata_wait(false) != 0) return -1;

    return 0;
}

bool ata_present(void) {
    return disk_present;
}

uint64_t ata_get_sector_count(void) {
    return disk_sector_count;
}

bool ata_get_solfs_region(uint64_t *start_lba, uint64_t *sector_count) {
    if (!disk_present || !start_lba || !sector_count)
        return false;

    uint64_t solis_start = 0;
    uint64_t solis_count = 0;
    int result = gpt_find_solis_region(&solis_start, &solis_count);
    if (result == 0) {
        *start_lba = solis_start;
        *sector_count = solis_count;
        return true;
    }
    if (result != 2)
        return false;

    uint8_t mbr[ATA_SECTOR_SIZE];
    if (ata_read_sector(0, mbr) != 0)
        return false;
    for (int i = 0; i < 4; i++) {
        if (mbr[446 + i * 16 + 4] != 0)
            return false;
    }
    if (mbr[510] == 0x55 && mbr[511] == 0xAA)
        return false;

    *start_lba = 0;
    *sector_count = disk_sector_count;
    return true;
}

const char* ata_get_serial(void) {
    return disk_serial;
}

const char* ata_get_model(void) {
    return disk_model;
}

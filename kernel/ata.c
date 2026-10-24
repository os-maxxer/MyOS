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

#define STATUS_BSY  0x80
#define STATUS_DRDY 0x40
#define STATUS_DRQ  0x08
#define STATUS_ERR  0x01

static bool disk_present = false;
static char disk_serial[21];
static char disk_model[41];

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

bool ata_init(void) {
    outb(ATA_DRIVE, 0xE0);
    ata_wait(false);

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);

    outb(ATA_COMMAND, ATA_CMD_IDENT);
    if (inb(ATA_STATUS) == 0) {
        disk_present = false;
        console_write("ATA: no drive detected.\n");
        return false;
    }

    if (ata_wait(false) != 0) {
        disk_present = false;
        console_write("ATA: drive timeout.\n");
        return false;
    }

    uint16_t id_buf[256];
    for (int i = 0; i < 256; i++)
        id_buf[i] = inw(ATA_DATA);

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

int ata_read_sector(uint32_t lba, uint8_t *buf) {
    if (!disk_present) return -1;

    if (ata_wait(false) != 0) return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_READ);

    if (ata_wait(true) != 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t val = inw(ATA_DATA);
        buf[i * 2] = val & 0xFF;
        buf[i * 2 + 1] = (val >> 8) & 0xFF;
    }

    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    if (!disk_present) return -1;

    if (ata_wait(false) != 0) return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_WRITE);

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

const char* ata_get_serial(void) {
    return disk_serial;
}

const char* ata_get_model(void) {
    return disk_model;
}

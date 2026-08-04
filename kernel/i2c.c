#include <solis/i2c.h>
#include <solis/pci.h>
#include <solis/ports.h>
#include <solis/console.h>

#define SMBUS_HST_STS      0x00
#define SMBUS_HST_CNT      0x02
#define SMBUS_HST_CMD      0x03
#define SMBUS_HST_ADDR     0x04
#define SMBUS_HST_DAT0     0x05
#define SMBUS_HST_DAT1     0x06
#define SMBUS_HST_BLOCK_DB 0x07

#define STS_HOST_BUSY  0x01
#define STS_INTERRUPT  0x02
#define STS_DEV_ERR    0x04
#define STS_FAILED     0x10
#define STS_BUS_ERR    0x20

#define CNT_START      0x40
#define CNT_LAST_BYTE  0x20

#define SMBUS_QUICK      0
#define SMBUS_BYTE       1
#define SMBUS_BYTE_DATA  2
#define SMBUS_WORD_DATA  3
#define SMBUS_BLOCK_DATA 5

#define TIMEOUT 100000

static uint16_t smbus_io_base = 0;
static bool i2c_initialized = false;

static int smbus_wait_idle(void) {
    uint32_t timeout = TIMEOUT;
    while (timeout--) {
        uint8_t sts = inb(smbus_io_base + SMBUS_HST_STS);
        if (!(sts & STS_HOST_BUSY)) {
            outb(smbus_io_base + SMBUS_HST_STS, sts);
            return 0;
        }
    }
    return -1;
}

static int smbus_transaction(uint8_t addr, uint8_t cmd, uint8_t prot, uint8_t *data, uint8_t block_len) {
    if (!smbus_io_base) return -1;
    if (smbus_wait_idle() != 0) return -1;

    outb(smbus_io_base + SMBUS_HST_STS, 0xFF);
    outb(smbus_io_base + SMBUS_HST_CMD, cmd);
    outb(smbus_io_base + SMBUS_HST_ADDR, (addr << 1) | 0x01);

    if (prot == SMBUS_BYTE_DATA || prot == SMBUS_WORD_DATA) {
        outb(smbus_io_base + SMBUS_HST_DAT0, data ? data[0] : 0);
        if (prot == SMBUS_WORD_DATA)
            outb(smbus_io_base + SMBUS_HST_DAT1, data ? data[1] : 0);
    }

    uint8_t cnt = prot;
    if (prot == SMBUS_BLOCK_DATA) cnt = SMBUS_BLOCK_DATA | CNT_LAST_BYTE;
    cnt |= CNT_START;
    outb(smbus_io_base + SMBUS_HST_CNT, cnt);

    uint32_t timeout = TIMEOUT;
    while (timeout--) {
        uint8_t sts = inb(smbus_io_base + SMBUS_HST_STS);
        if (sts & STS_DEV_ERR) { outb(smbus_io_base + SMBUS_HST_STS, 0xFF); return -1; }
        if (sts & STS_FAILED)  { outb(smbus_io_base + SMBUS_HST_STS, 0xFF); return -1; }
        if (sts & STS_BUS_ERR) { outb(smbus_io_base + SMBUS_HST_STS, 0xFF); return -1; }
        if (sts & STS_INTERRUPT) break;
    }
    if (timeout == 0) return -1;

    if (prot == SMBUS_BYTE_DATA || prot == SMBUS_WORD_DATA) {
        data[0] = inb(smbus_io_base + SMBUS_HST_DAT0);
        if (prot == SMBUS_WORD_DATA)
            data[1] = inb(smbus_io_base + SMBUS_HST_DAT1);
    } else if (prot == SMBUS_BLOCK_DATA) {
        for (int i = 0; i < block_len && i < 32; i++)
            data[i] = inb(smbus_io_base + SMBUS_HST_BLOCK_DB + i);
    }

    outb(smbus_io_base + SMBUS_HST_STS, 0xFF);
    return 0;
}

void i2c_init(void) {
    uint16_t slot = pci_find_device(I2C_SMBUS_PCI_VENDOR, I2C_SMBUS_PCI_DEVICE);
    if (slot == 0xFFFF) {
        uint16_t alt_ids[] = {0x27DA, 0x269B, 0x811A, 0x8C22, 0x9C22, 0x5AA4, 0};
        for (int i = 0; alt_ids[i]; i++) {
            slot = pci_find_device(I2C_SMBUS_PCI_VENDOR, alt_ids[i]);
            if (slot != 0xFFFF) break;
        }
    }
    if (slot == 0xFFFF) {
        return;
    }

    uint32_t bar = pci_get_bar(0, slot, 0, 4);
    smbus_io_base = bar & 0xFFFE;
    if (smbus_io_base == 0) {
        bar = pci_get_bar(0, slot, 0, 0);
        smbus_io_base = bar & 0xFFFE;
    }

    i2c_initialized = true;
}

bool i2c_detect_device(uint16_t address) {
    if (!i2c_initialized) return false;
    if (smbus_wait_idle() != 0) return false;
    outb(smbus_io_base + SMBUS_HST_STS, 0xFF);
    outb(smbus_io_base + SMBUS_HST_ADDR, (address << 1) | 0x01);
    outb(smbus_io_base + SMBUS_HST_CNT, SMBUS_QUICK | CNT_START);

    uint32_t timeout = TIMEOUT;
    while (timeout--) {
        uint8_t sts = inb(smbus_io_base + SMBUS_HST_STS);
        if (sts & STS_DEV_ERR) { outb(smbus_io_base + SMBUS_HST_STS, 0xFF); return false; }
        if (sts & STS_INTERRUPT) break;
    }
    outb(smbus_io_base + SMBUS_HST_STS, 0xFF);
    return timeout > 0;
}

int i2c_smbus_read_byte(uint16_t address, uint8_t command, uint8_t *data) {
    if (!i2c_initialized || !data) return -1;
    uint8_t buf[2] = {0, 0};
    int ret = smbus_transaction(address, command, SMBUS_BYTE_DATA, buf, 0);
    if (ret == 0) *data = buf[0];
    return ret;
}

int i2c_smbus_write_byte(uint16_t address, uint8_t command, uint8_t data) {
    if (!i2c_initialized) return -1;
    uint8_t buf[2] = {data, 0};
    return smbus_transaction(address, command, SMBUS_BYTE_DATA, buf, 0);
}

int i2c_smbus_read_block(uint16_t address, uint8_t command, uint8_t *buf, uint8_t len) {
    if (!i2c_initialized || !buf || len == 0) return -1;
    return smbus_transaction(address, command, SMBUS_BLOCK_DATA, buf, len);
}

bool i2c_device_present(uint16_t address) {
    return i2c_detect_device(address);
}

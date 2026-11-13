#include <solis/i2c.h>
#include <solis/pci.h>
#include <solis/ports.h>
#include <solis/console.h>
#include <solis/dbg.h>
#include <solis/pmm.h>
#include <solis/vmm.h>

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
#define PIIX4_STS_BUS_ERR 0x08

#define CNT_START      0x40
#define CNT_LAST_BYTE  0x20
#define PIIX4_BYTE_DATA 0x08
#define PIIX4_WORD_DATA 0x0C
#define PIIX4_BLOCK_DATA 0x14

#define SMBUS_QUICK      0x00
#define SMBUS_BYTE       0x01
#define SMBUS_BYTE_DATA  0x02
#define SMBUS_WORD_DATA  0x03
#define SMBUS_BLOCK_DATA 0x05

#define AMD_SMBUS_INDEX_PORT 0x0CD6
#define AMD_SMBUS_DATA_PORT  0x0CD7
#define AMD_SMBUS_DEVICE_HUDSON2 0x780B
#define AMD_SMBUS_DEVICE_KERNCZ  0x790B
#define AMD_FCH_PM_BASE 0xFED80300U

#define TIMEOUT 100000

static uint16_t smbus_io_base = 0;
static bool i2c_initialized = false;
static bool smbus_piix4_compatible = false;

static uint8_t smbus_error_mask(void) {
    return STS_DEV_ERR | STS_FAILED |
           (smbus_piix4_compatible ? PIIX4_STS_BUS_ERR : STS_BUS_ERR);
}

static uint8_t smbus_control_protocol(uint8_t protocol) {
    if (!smbus_piix4_compatible) return protocol;
    if (protocol == SMBUS_BYTE_DATA) return PIIX4_BYTE_DATA;
    if (protocol == SMBUS_WORD_DATA) return PIIX4_WORD_DATA;
    if (protocol == SMBUS_BLOCK_DATA) return PIIX4_BLOCK_DATA;
    return protocol;
}

static bool smbus_find_controller(struct pci_device_info *device) {
    uint16_t ids[] = {
        0x2930, 0x27DA, 0x269B, 0x811A, 0x8C22, 0x9C22, 0x5AA4,
        0x9D4E, 0xA123, 0xA1A3, 0xA0F0, 0xA1F0, 0xA1A1, 0
    };

    for (int i = 0; ids[i] != 0; i++) {
        if (pci_find_device(I2C_SMBUS_PCI_VENDOR, ids[i], device))
            return true;
    }

    return pci_find_class(0x0C, 0x05, 0x00, device) &&
           (pci_read_config(device->bus, device->slot, device->function, 0) &
            0xFFFF) == I2C_SMBUS_PCI_VENDOR;
}

static bool smbus_find_amd_controller(struct pci_device_info *device,
                                      uint16_t *device_id) {
    static const uint16_t ids[] = {
        AMD_SMBUS_DEVICE_KERNCZ,
        AMD_SMBUS_DEVICE_HUDSON2,
        0
    };

    for (uint32_t i = 0; ids[i] != 0; i++) {
        if (pci_find_device(0x1022, ids[i], device)) {
            *device_id = ids[i];
            return true;
        }
    }
    return false;
}

static uint8_t amd_smbus_config_read(uint8_t index, bool use_mmio) {
    if (use_mmio) {
        volatile uint8_t *config =
            (volatile uint8_t *)(uintptr_t)AMD_FCH_PM_BASE;
        return config[index];
    }
    outb(AMD_SMBUS_INDEX_PORT, index);
    return inb(AMD_SMBUS_DATA_PORT);
}

static void smbus_clear_status(uint8_t status) {
    outb(smbus_io_base + SMBUS_HST_STS,
         status & (STS_INTERRUPT | smbus_error_mask()));
}

static int smbus_wait_idle(void) {
    uint32_t timeout = TIMEOUT;
    while (timeout--) {
        uint8_t sts = inb(smbus_io_base + SMBUS_HST_STS);
        if (sts & smbus_error_mask()) {
            smbus_clear_status(sts);
            return -1;
        }
        if (!(sts & STS_HOST_BUSY)) return 0;
    }
    return -1;
}

static int smbus_transaction(uint8_t addr, uint8_t cmd, uint8_t prot,
                             uint8_t *data, uint8_t block_len,
                             bool read_transfer) {
    if (!i2c_initialized || addr > 0x7F ||
        (prot != SMBUS_BYTE_DATA && prot != SMBUS_WORD_DATA &&
         prot != SMBUS_BLOCK_DATA) ||
        !data || (prot == SMBUS_BLOCK_DATA &&
                  (block_len == 0 || block_len > 32)))
        return -1;
    if (smbus_wait_idle() != 0) return -1;

    smbus_clear_status(0xFF);
    outb(smbus_io_base + SMBUS_HST_CMD, cmd);
    outb(smbus_io_base + SMBUS_HST_ADDR,
         (uint8_t)((addr << 1) | (read_transfer ? 1 : 0)));

    if (!read_transfer &&
        (prot == SMBUS_BYTE_DATA || prot == SMBUS_WORD_DATA)) {
        outb(smbus_io_base + SMBUS_HST_DAT0, data[0]);
        if (prot == SMBUS_WORD_DATA)
            outb(smbus_io_base + SMBUS_HST_DAT1, data[1]);
    }

    uint8_t cnt = smbus_control_protocol(prot);
    if (prot == SMBUS_BLOCK_DATA && !smbus_piix4_compatible)
        cnt |= CNT_LAST_BYTE;
    cnt |= CNT_START;
    outb(smbus_io_base + SMBUS_HST_CNT, cnt);

    uint32_t timeout = TIMEOUT;
    uint8_t status = 0;
    while (timeout--) {
        status = inb(smbus_io_base + SMBUS_HST_STS);
        if (status & smbus_error_mask()) {
            smbus_clear_status(status);
            return -1;
        }
        if ((status & STS_INTERRUPT) && !(status & STS_HOST_BUSY))
            break;
    }
    if (timeout == 0 || !(status & STS_INTERRUPT)) {
        smbus_clear_status(status);
        return -1;
    }

    if (read_transfer &&
        (prot == SMBUS_BYTE_DATA || prot == SMBUS_WORD_DATA)) {
        data[0] = inb(smbus_io_base + SMBUS_HST_DAT0);
        if (prot == SMBUS_WORD_DATA) {
            data[1] = inb(smbus_io_base + SMBUS_HST_DAT1);
        }
    } else if (read_transfer && prot == SMBUS_BLOCK_DATA) {
        uint8_t len = inb(smbus_io_base + SMBUS_HST_DAT0);
        if (len == 0 || len > block_len) {
            smbus_clear_status(status);
            return -1;
        }
        if (smbus_piix4_compatible)
            (void)inb(smbus_io_base + SMBUS_HST_CNT);
        for (uint8_t i = 0; i < len; i++) {
            data[i] = inb(smbus_io_base + SMBUS_HST_BLOCK_DB);
        }
    }

    smbus_clear_status(status);
    return 0;
}

void i2c_init(void) {
    smbus_io_base = 0;
    i2c_initialized = false;
    smbus_piix4_compatible = false;
    dbg_set_driver_state("i2c", DBG_DRIVER_UNINITIALIZED);
    struct pci_device_info device;
    if (!smbus_find_controller(&device)) {
        uint16_t amd_device_id = 0;
        if (!smbus_find_amd_controller(&device, &amd_device_id)) {
            dbg_print("[I2C] No supported SMBus controller found\n");
            dbg_set_driver_state("i2c", DBG_DRIVER_FAILED);
            return;
        }

        uint8_t revision = pci_read_config(device.bus, device.slot,
                                           device.function, 0x08) & 0xFF;
        bool kerncz = amd_device_id == AMD_SMBUS_DEVICE_KERNCZ;
        bool use_mmio = kerncz && revision >= 0x51;
        uint8_t config_index =
            (kerncz && revision >= 0x49) ? 0x00 : 0x2C;
        if (use_mmio &&
            !vmm_map_page(AMD_FCH_PM_BASE & ~(PMM_PAGE_SIZE - 1),
                          AMD_FCH_PM_BASE & ~(PMM_PAGE_SIZE - 1),
                          VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE |
                              VMM_PAGE_CACHE_DISABLE)) {
            dbg_print("[I2C] Could not map AMD FCH SMBus configuration registers\n");
            dbg_set_driver_state("i2c", DBG_DRIVER_FAILED);
            return;
        }
        uint8_t base_low = amd_smbus_config_read(config_index, use_mmio);
        uint8_t base_high = amd_smbus_config_read(config_index + 1, use_mmio);
        bool controller_enabled =
            (kerncz && revision >= 0x49) ? (base_low & 0x10) != 0
                                         : (base_low & 0x01) != 0;
        uint32_t io_base =
            (kerncz && revision >= 0x49) ? (uint32_t)base_high << 8
                                         : (((uint32_t)base_high << 8) |
                                            base_low) & 0xFFE0;

        if (!controller_enabled || io_base == 0 ||
            io_base + SMBUS_HST_BLOCK_DB > 0xFFFF ||
            !pci_enable_command_bits(&device, PCI_COMMAND_IO_SPACE)) {
            dbg_print("[I2C] AMD SMBus controller is disabled or has no valid I/O base\n");
            dbg_set_driver_state("i2c", DBG_DRIVER_FAILED);
            return;
        }

        smbus_io_base = (uint16_t)io_base;
        smbus_piix4_compatible = true;
        i2c_initialized = true;
        dbg_print("[I2C] AMD-compatible SMBus active at I/O 0x");
        dbg_print_hex(smbus_io_base);
        dbg_print("\n");
        dbg_set_driver_state("i2c", DBG_DRIVER_ACTIVE);
        return;
    }

    smbus_io_base = 0;
    for (int bar_index = 0; bar_index < 6; bar_index++) {
        uint32_t bar = pci_get_bar(device.bus, device.slot,
                                   device.function, bar_index);
        if ((bar & 1) != 0) {
            uint32_t io_base = bar & 0xFFFFFFFC;
            if (io_base != 0 && io_base <= 0xFFFF) {
                smbus_io_base = (uint16_t)io_base;
                break;
            }
        } else if ((bar & 0x6) == 0x4) {
            bar_index++;
        }
    }

    if (smbus_io_base == 0 ||
        !pci_enable_command_bits(&device, PCI_COMMAND_IO_SPACE)) {
        dbg_print("[I2C] Could not enable SMBus I/O space\n");
        dbg_set_driver_state("i2c", DBG_DRIVER_FAILED);
        return;
    }

    uint32_t host_config = pci_read_config(device.bus, device.slot,
                                           device.function, 0x40);
    pci_write_config(device.bus, device.slot, device.function, 0x40,
                     host_config | 1U);
    host_config = pci_read_config(device.bus, device.slot,
                                  device.function, 0x40);
    if (!(host_config & 1U)) {
        dbg_print("[I2C] SMBus host controller is disabled\n");
        smbus_io_base = 0;
        dbg_set_driver_state("i2c", DBG_DRIVER_FAILED);
        return;
    }

    i2c_initialized = true;
    dbg_print("[I2C] Intel SMBus active at I/O 0x");
    dbg_print_hex(smbus_io_base);
    dbg_print("\n");
    dbg_set_driver_state("i2c", DBG_DRIVER_ACTIVE);
}

bool i2c_detect_device(uint16_t address) {
    if (!i2c_initialized || address > 0x7F) return false;
    if (smbus_wait_idle() != 0) return false;
    smbus_clear_status(0xFF);
    outb(smbus_io_base + SMBUS_HST_ADDR, (address << 1) | 0x01);
    outb(smbus_io_base + SMBUS_HST_CNT, SMBUS_QUICK | CNT_START);

    uint32_t timeout = TIMEOUT;
    uint8_t status = 0;
    while (timeout--) {
        status = inb(smbus_io_base + SMBUS_HST_STS);
        if (status & smbus_error_mask()) {
            smbus_clear_status(status);
            return false;
        }
        if ((status & STS_INTERRUPT) && !(status & STS_HOST_BUSY))
            break;
    }
    bool complete = timeout > 0 && (status & STS_INTERRUPT) &&
                    !(status & STS_HOST_BUSY);
    smbus_clear_status(status);
    return complete;
}

int i2c_smbus_read_byte(uint16_t address, uint8_t command, uint8_t *data) {
    if (!i2c_initialized || !data) return -1;
    uint8_t buf[2] = {0, 0};
    int ret = smbus_transaction(address, command, SMBUS_BYTE_DATA, buf, 0,
                                true);
    if (ret == 0) *data = buf[0];
    return ret;
}

int i2c_smbus_write_byte(uint16_t address, uint8_t command, uint8_t data) {
    if (!i2c_initialized) return -1;
    uint8_t buf[2] = {data, 0};
    return smbus_transaction(address, command, SMBUS_BYTE_DATA, buf, 0,
                             false);
}

int i2c_smbus_read_block(uint16_t address, uint8_t command, uint8_t *buf, uint8_t len) {
    if (!i2c_initialized || !buf || len == 0) return -1;
    return smbus_transaction(address, command, SMBUS_BLOCK_DATA, buf, len,
                             true);
}

bool i2c_device_present(uint16_t address) {
    return i2c_detect_device(address);
}

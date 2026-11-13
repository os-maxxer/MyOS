#include <solis/ahci.h>
#include <solis/console.h>
#include <solis/pci.h>
#include <solis/pmm.h>
#include <solis/vmm.h>

#define AHCI_ABAR_BYTES 0x2000
#define AHCI_PORT_BASE  0x100
#define AHCI_PORT_STRIDE 0x80
#define AHCI_MAX_PORTS  32
#define AHCI_SECTOR_SIZE 512

#define AHCI_SIG_ATA   0x00000101
#define AHCI_SIG_ATAPI 0xEB140101

#define PXCMD_ST   (1U << 0)
#define PXCMD_FRE  (1U << 4)
#define PXCMD_FR   (1U << 14)
#define PXCMD_CR   (1U << 15)

#define PXIS_TFES  (1U << 30)

#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_IDENTIFY      0xEC

#define PCI_COMMAND_MEMORY 0x0002

struct ahci_port {
    volatile uint32_t *regs;
    uint8_t *cmd_list;
    uint8_t *fis;
    uint8_t *cmd_table;
    uint64_t sector_count;
    char serial[21];
    char model[41];
    bool active;
};

static struct ahci_port disk_port;
static bool ahci_initialized = false;
static bool ahci_active = false;

static void ahci_zero(void *memory, uint32_t size) {
    uint8_t *bytes = (uint8_t *)memory;
    for (uint32_t i = 0; i < size; i++)
        bytes[i] = 0;
}

static void ahci_write16(uint8_t *memory, uint16_t value) {
    memory[0] = (uint8_t)value;
    memory[1] = (uint8_t)(value >> 8);
}

static void ahci_write32(uint8_t *memory, uint32_t value) {
    memory[0] = (uint8_t)value;
    memory[1] = (uint8_t)(value >> 8);
    memory[2] = (uint8_t)(value >> 16);
    memory[3] = (uint8_t)(value >> 24);
}

static void ahci_write64(uint8_t *memory, uint64_t value) {
    ahci_write32(memory, (uint32_t)value);
    ahci_write32(memory + 4, (uint32_t)(value >> 32));
}

static uint64_t ahci_physical(const void *memory) {
    return (uint64_t)(uintptr_t)memory;
}

static bool ahci_map_mmio(uint64_t physical, volatile uint8_t **mapped) {
    uint64_t start = physical & ~(uint64_t)(PMM_PAGE_SIZE - 1);
    uint64_t end = (physical + AHCI_ABAR_BYTES + PMM_PAGE_SIZE - 1) &
                   ~(uint64_t)(PMM_PAGE_SIZE - 1);
    if (physical >= PMM_DIRECT_MAP_LIMIT || end < physical ||
        end > PMM_DIRECT_MAP_LIMIT)
        return false;

    for (uint64_t page = start; page < end; page += PMM_PAGE_SIZE) {
        if (!vmm_map_page((uintptr_t)page, page,
                          VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE |
                              VMM_PAGE_CACHE_DISABLE))
            return false;
    }
    *mapped = (volatile uint8_t *)(uintptr_t)physical;
    return true;
}

static bool ahci_get_bar(const struct pci_device_info *device,
                         uint64_t *base) {
    uint32_t low = pci_get_bar(device->bus, device->slot,
                               device->function, 5);
    if ((low & 1U) || low == 0 || ((low >> 1) & 3U) != 0)
        return false;
    *base = (uint64_t)(low & ~0xFULL);
    return *base != 0;
}

static bool ahci_engine_start(struct ahci_port *port) {
    uint32_t command = port->regs[6];
    command &= ~(PXCMD_ST | PXCMD_FRE);
    port->regs[6] = command;

    uint32_t wait = 0;
    while (wait++ < 1000000) {
        if (!(port->regs[6] & (PXCMD_FR | PXCMD_CR)))
            break;
    }
    if (wait >= 1000000)
        return false;

    port->regs[0] = (uint32_t)ahci_physical(port->cmd_list);
    port->regs[1] = (uint32_t)(ahci_physical(port->cmd_list) >> 32);
    port->regs[2] = (uint32_t)ahci_physical(port->fis);
    port->regs[3] = (uint32_t)(ahci_physical(port->fis) >> 32);
    port->regs[4] = 0xFFFFFFFFU;
    port->regs[5] = 0;
    port->regs[13] = 0;
    port->regs[14] = 0;

    command = port->regs[6];
    port->regs[6] = command | PXCMD_FRE;
    wait = 0;
    while (wait++ < 1000000) {
        if (port->regs[6] & PXCMD_FR)
            break;
    }
    if (wait >= 1000000)
        return false;

    command = port->regs[6];
    port->regs[6] = command | PXCMD_ST;
    return true;
}

static bool ahci_issue(struct ahci_port *port, uint8_t command,
                       uint64_t lba, uint16_t count, uint8_t *buffer,
                       bool write, uint32_t bytes) {
    if (!port->active || !buffer || bytes == 0)
        return false;

    ahci_zero(port->cmd_table, 0x90);
    uint8_t *fis = port->cmd_table;
    fis[0] = 0x27;              /* Host to Device Register FIS. */
    fis[1] = 0x80;              /* Command, no port multiplier. */
    fis[2] = command;

    if (command != ATA_CMD_IDENTIFY) {
        fis[4] = (uint8_t)lba;
        fis[5] = (uint8_t)(lba >> 8);
        fis[6] = (uint8_t)(lba >> 16);
        fis[7] = 0x40;          /* LBA mode. */
        fis[8] = (uint8_t)(lba >> 24);
        fis[9] = (uint8_t)(lba >> 32);
        fis[10] = (uint8_t)(lba >> 40);
        fis[12] = (uint8_t)count;
        fis[13] = (uint8_t)(count >> 8);
    }

    uint8_t *prdt = port->cmd_table + 0x80;
    ahci_write64(prdt, ahci_physical(buffer));
    ahci_write64(prdt + 8, 0);
    ahci_write32(prdt + 12, (bytes - 1) & 0x003FFFFFU);

    uint8_t *header = port->cmd_list;
    ahci_zero(header, 32);
    header[0] = (uint8_t)(5 | (write ? (1U << 6) : 0));
    ahci_write16(header + 2, 1); /* One PRDT entry. */
    ahci_write64(header + 8, ahci_physical(port->cmd_table));

    __asm__ volatile("mfence" : : : "memory");
    port->regs[4] = 0xFFFFFFFFU;
    port->regs[14] = 1;

    uint32_t spin = 0;
    while (spin++ < 50000000) {
        if (port->regs[4] & PXIS_TFES) {
            port->regs[4] = 0xFFFFFFFFU;
            return false;
        }
        if (!(port->regs[14] & 1))
            break;
        __asm__ volatile("pause");
    }
    if (spin >= 50000000) {
        port->regs[4] = 0xFFFFFFFFU;
        return false;
    }

    uint32_t status = port->regs[4];
    uint32_t task_file = port->regs[8];
    port->regs[4] = 0xFFFFFFFFU;
    if (status & PXIS_TFES)
        return false;
    if (task_file & 0x01)
        return false;

    __asm__ volatile("mfence" : : : "memory");
    return true;
}

static bool ahci_parse_identify(struct ahci_port *port,
                                const uint8_t *identity) {
    uint16_t words[256];
    for (int i = 0; i < 256; i++)
        words[i] = (uint16_t)identity[i * 2] |
                   ((uint16_t)identity[i * 2 + 1] << 8);

    if (words[0] == 0xEB14 || words[0] == 0x9669)
        return false;

    bool lba48 = (words[83] & (1U << 10)) != 0;
    uint64_t sectors = 0;
    if (lba48) {
        sectors = (uint64_t)words[100]
                | ((uint64_t)words[101] << 16)
                | ((uint64_t)words[102] << 32)
                | ((uint64_t)words[103] << 48);
    } else if (words[49] & (1U << 9)) {
        sectors = (uint64_t)words[60]
                | ((uint64_t)words[61] << 16);
    }
    if (!sectors)
        return false;
    port->sector_count = sectors;

    for (int i = 0; i < 20; i++)
        port->serial[i] = ' ';
    port->serial[20] = '\0';
    for (int i = 0; i < 10; i++) {
        port->serial[i * 2] = (char)(words[10 + i] & 0xFF);
        port->serial[i * 2 + 1] = (char)(words[10 + i] >> 8);
    }
    for (int i = 19; i >= 0; i--) {
        if (port->serial[i] == ' ') port->serial[i] = '\0';
        else break;
    }

    for (int i = 0; i < 20; i++) {
        port->model[i * 2] = (char)(words[27 + i] >> 8);
        port->model[i * 2 + 1] = (char)(words[27 + i] & 0xFF);
    }
    port->model[40] = '\0';
    for (int i = 39; i >= 0; i--) {
        if (port->model[i] == ' ' || port->model[i] == '\0')
            port->model[i] = '\0';
        else
            break;
    }
    return true;
}

static bool ahci_port_init(struct ahci_port *port) {
    port->cmd_list = (uint8_t *)pmm_alloc_page();
    port->fis = (uint8_t *)pmm_alloc_page();
    port->cmd_table = (uint8_t *)pmm_alloc_page();
    if (!port->cmd_list || !port->fis || !port->cmd_table)
        return false;

    ahci_zero(port->cmd_list, PMM_PAGE_SIZE);
    ahci_zero(port->fis, PMM_PAGE_SIZE);
    ahci_zero(port->cmd_table, PMM_PAGE_SIZE);

    if (!ahci_engine_start(port))
        return false;
    port->active = true;

    uint8_t identity[AHCI_SECTOR_SIZE];
    if (!ahci_issue(port, ATA_CMD_IDENTIFY, 0, 0, identity, false,
                    sizeof(identity)) ||
        !ahci_parse_identify(port, identity)) {
        port->active = false;
        return false;
    }
    return true;
}

bool ahci_init(void) {
    if (ahci_initialized)
        return ahci_active;
    ahci_initialized = true;
    ahci_active = false;

    struct pci_device_info device;
    if (!pci_find_class(0x01, 0x06, 0x01, &device))
        return false;

    if (!pci_enable_command_bits(&device,
                                 PCI_COMMAND_MEMORY |
                                     PCI_COMMAND_BUS_MASTER)) {
        console_write("AHCI: could not enable memory and bus mastering.\n");
        return false;
    }

    uint64_t bar = 0;
    volatile uint8_t *mmio = 0;
    if (!ahci_get_bar(&device, &bar) || !ahci_map_mmio(bar, &mmio)) {
        console_write("AHCI: could not map the controller registers.\n");
        return false;
    }

    volatile uint32_t *registers = (volatile uint32_t *)mmio;
    uint32_t ports = registers[3];
    if (!ports) {
        console_write("AHCI: controller reports no ports.\n");
        return false;
    }

    registers[1] |= (1U << 31); /* AHCI enable. */
    uint32_t handoff = registers[10];
    if (handoff & 1U) {
        registers[10] = handoff | 2U;
        uint32_t wait = 0;
        while (wait++ < 1000000 && (registers[10] & 1U)) {
            __asm__ volatile("pause");
        }
    }

    for (uint32_t index = 0; index < AHCI_MAX_PORTS; index++) {
        if (!(ports & (1U << index)))
            continue;

        volatile uint32_t *port_regs =
            (volatile uint32_t *)(mmio + AHCI_PORT_BASE +
                                  index * AHCI_PORT_STRIDE);
        uint32_t ssts = port_regs[10];
        if ((ssts & 0x0F) != 3 || ((ssts >> 8) & 0x0F) != 1)
            continue;
        if (port_regs[9] != AHCI_SIG_ATA)
            continue;

        ahci_zero(&disk_port, sizeof(disk_port));
        disk_port.regs = port_regs;
        if (!ahci_port_init(&disk_port))
            continue;

        ahci_active = true;
        console_write("AHCI: SATA disk on port ");
        console_write_dec(index);
        console_write("\n");
        return true;
    }

    console_write("AHCI: no usable SATA disk found.\n");
    return false;
}

bool ahci_present(void) {
    return ahci_active;
}

int ahci_read_sector(uint64_t lba, uint8_t *buf) {
    if (!ahci_active || !buf || lba >= disk_port.sector_count)
        return -1;
    return ahci_issue(&disk_port, ATA_CMD_READ_DMA_EXT, lba, 1, buf,
                      false, AHCI_SECTOR_SIZE) ? 0 : -1;
}

int ahci_write_sector(uint64_t lba, const uint8_t *buf) {
    if (!ahci_active || !buf || lba >= disk_port.sector_count)
        return -1;
    return ahci_issue(&disk_port, ATA_CMD_WRITE_DMA_EXT, lba, 1,
                      (uint8_t *)buf, true, AHCI_SECTOR_SIZE) ? 0 : -1;
}

uint64_t ahci_get_sector_count(void) {
    return ahci_active ? disk_port.sector_count : 0;
}

const char *ahci_get_serial(void) {
    return disk_port.serial;
}

const char *ahci_get_model(void) {
    return disk_port.model;
}

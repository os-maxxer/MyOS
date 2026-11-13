#include <solis/pci.h>
#include <solis/ports.h>
#include <stdbool.h>

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static bool pci_device_exists(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t id = pci_read_config(bus, slot, func, 0x00);
    return (id & 0xFFFF) != 0xFFFF;
}

static int pci_function_count(uint8_t bus, uint8_t slot) {
    uint32_t header = pci_read_config(bus, slot, 0, 0x0C);
    return (header & (0x80U << 16)) ? 8 : 1;
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id,
                     struct pci_device_info *device) {
    if (!device) return false;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            if (!pci_device_exists((uint8_t)bus, (uint8_t)slot, 0)) continue;
            int function_count = pci_function_count((uint8_t)bus, (uint8_t)slot);
            for (int func = 0; func < function_count; func++) {
                if (!pci_device_exists((uint8_t)bus, (uint8_t)slot, (uint8_t)func)) continue;
                uint32_t id = pci_read_config((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                uint16_t vendor = id & 0xFFFF;
                uint16_t found_device_id = id >> 16;
                if (vendor == vendor_id && found_device_id == device_id) {
                    device->bus = (uint8_t)bus;
                    device->slot = (uint8_t)slot;
                    device->function = (uint8_t)func;
                    return true;
                }
            }
        }
    }
    return false;
}

bool pci_find_class(uint8_t base_class, uint8_t sub_class, uint8_t prog_if,
                    struct pci_device_info *device) {
    if (!device) return false;

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            if (!pci_device_exists((uint8_t)bus, (uint8_t)slot, 0)) continue;
            int function_count = pci_function_count((uint8_t)bus, (uint8_t)slot);
            for (int func = 0; func < function_count; func++) {
                if (!pci_device_exists((uint8_t)bus, (uint8_t)slot, (uint8_t)func)) continue;
                uint32_t cfg = pci_read_config((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                uint8_t cls = (cfg >> 24) & 0xFF;
                uint8_t sub = (cfg >> 16) & 0xFF;
                uint8_t pif = (cfg >> 8) & 0xFF;
                if (cls == base_class && sub == sub_class && pif == prog_if) {
                    device->bus = (uint8_t)bus;
                    device->slot = (uint8_t)slot;
                    device->function = (uint8_t)func;
                    return true;
                }
            }
        }
    }
    return false;
}

bool pci_enable_command_bits(const struct pci_device_info *device, uint16_t bits) {
    if (!device) return false;

    uint32_t command_status = pci_read_config(device->bus, device->slot,
                                              device->function, 0x04);
    uint16_t command = (uint16_t)command_status;
    command |= bits;
    pci_write_config(device->bus, device->slot, device->function, 0x04, command);

    command_status = pci_read_config(device->bus, device->slot,
                                     device->function, 0x04);
    return ((uint16_t)command_status & bits) == bits;
}

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index) {
    if (bar_index < 0 || bar_index >= 6) return 0;
    return pci_read_config(bus, slot, func, 0x10 + bar_index * 4);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

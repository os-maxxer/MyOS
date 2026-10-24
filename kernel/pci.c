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

uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                if (!pci_device_exists((uint8_t)bus, (uint8_t)slot, (uint8_t)func)) continue;
                uint32_t id = pci_read_config((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                uint16_t vendor = id & 0xFFFF;
                uint16_t device = id >> 16;
                if (vendor == vendor_id && device == device_id) {
                    return ((uint16_t)slot << 8) | (uint16_t)func;
                }
            }
        }
    }
    return 0xFFFF;
}

uint16_t pci_find_class(uint8_t base_class, uint8_t sub_class, uint8_t prog_if) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                if (!pci_device_exists((uint8_t)bus, (uint8_t)slot, (uint8_t)func)) continue;
                uint32_t cfg = pci_read_config((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                uint8_t cls = (cfg >> 24) & 0xFF;
                uint8_t sub = (cfg >> 16) & 0xFF;
                uint8_t pif = (cfg >> 8) & 0xFF;
                if (cls == base_class && sub == sub_class && pif == prog_if) {
                    return ((uint16_t)slot << 8) | (uint16_t)func;
                }
            }
        }
    }
    return 0xFFFF;
}

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index) {
    return pci_read_config(bus, slot, func, 0x10 + bar_index * 4);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

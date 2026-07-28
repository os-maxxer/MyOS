#include <nyx/pci.h>
#include <nyx/ports.h>

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (uint16_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_read_config(0, slot, 0, 0);
        uint16_t vendor = id & 0xFFFF;
        uint16_t device = id >> 16;
        if (vendor == vendor_id && device == device_id)
            return slot;
    }
    return 0xFFFF;
}

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index) {
    return pci_read_config(bus, slot, func, 0x10 + bar_index * 4);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

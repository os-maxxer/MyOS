#ifndef SOLIS_PCI_H
#define SOLIS_PCI_H

#include <solis/types.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

#define PCI_COMMAND_IO_SPACE   0x0001
#define PCI_COMMAND_BUS_MASTER 0x0004

struct pci_device_info {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
};

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
bool pci_find_device(uint16_t vendor_id, uint16_t device_id,
                     struct pci_device_info *device);
bool pci_find_class(uint8_t base_class, uint8_t sub_class, uint8_t prog_if,
                   struct pci_device_info *device);
bool pci_enable_command_bits(const struct pci_device_info *device, uint16_t bits);
uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

#endif

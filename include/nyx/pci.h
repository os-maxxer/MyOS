#ifndef NYX_PCI_H
#define NYX_PCI_H

#include <nyx/types.h>

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id);
uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_index);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

#endif

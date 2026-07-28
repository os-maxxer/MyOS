#ifndef NYX_I2C_H
#define NYX_I2C_H

#include <nyx/types.h>
#include <stdbool.h>

#define I2C_SMBUS_PCI_VENDOR 0x8086
#define I2C_SMBUS_PCI_DEVICE 0x2930

struct i2c_device {
    uint16_t address;
    bool present;
};

void i2c_init(void);
bool i2c_detect_device(uint16_t address);
int i2c_smbus_read_byte(uint16_t address, uint8_t command, uint8_t *data);
int i2c_smbus_write_byte(uint16_t address, uint8_t command, uint8_t data);
int i2c_smbus_read_block(uint16_t address, uint8_t command, uint8_t *buf, uint8_t len);
bool i2c_device_present(uint16_t address);

#endif

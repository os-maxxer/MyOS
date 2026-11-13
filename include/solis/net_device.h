#ifndef SOLIS_NET_DEVICE_H
#define SOLIS_NET_DEVICE_H

#include <solis/types.h>
#include <stdbool.h>

#define NET_DEVICE_NAME_MAX 16

struct net_device {
    char name[NET_DEVICE_NAME_MAX];
    uint8_t mac[6];
    uint32_t max_frame_size;
    int (*send_frame)(const uint8_t *frame, uint32_t length);
    int (*receive_frame)(uint8_t *frame, uint32_t capacity);
};

bool net_device_register(const struct net_device *device);
bool net_device_available(void);
const char *net_device_name(void);
int net_device_send(const uint8_t *frame, uint32_t length);
int net_device_receive(uint8_t *frame, uint32_t capacity);
bool net_device_get_mac(uint8_t mac[6]);

#endif

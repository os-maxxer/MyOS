#include <solis/net_device.h>

static struct net_device active_device;
static bool device_registered;

bool net_device_register(const struct net_device *device) {
    if (!device || !device->send_frame || !device->receive_frame ||
        device->max_frame_size < 14 || device->name[0] == '\0')
        return false;

    active_device = *device;
    active_device.name[NET_DEVICE_NAME_MAX - 1] = '\0';
    device_registered = true;
    return true;
}

bool net_device_available(void) {
    return device_registered;
}

const char *net_device_name(void) {
    return device_registered ? active_device.name : "";
}

int net_device_send(const uint8_t *frame, uint32_t length) {
    if (!device_registered || !frame || length < 14 ||
        length > active_device.max_frame_size)
        return -1;
    return active_device.send_frame(frame, length);
}

int net_device_receive(uint8_t *frame, uint32_t capacity) {
    if (!device_registered || !frame || capacity < 14)
        return -1;
    if (capacity > active_device.max_frame_size)
        capacity = active_device.max_frame_size;
    return active_device.receive_frame(frame, capacity);
}

bool net_device_get_mac(uint8_t mac[6]) {
    if (!device_registered || !mac)
        return false;
    for (int i = 0; i < 6; i++)
        mac[i] = active_device.mac[i];
    return true;
}

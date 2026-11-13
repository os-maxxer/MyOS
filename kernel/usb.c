#include <solis/usb.h>
#include <solis/pci.h>
#include <solis/pmm.h>
#include <solis/vmm.h>
#include <solis/ports.h>
#include <solis/keyboard.h>
#include <solis/mouse.h>
#include <solis/timer.h>
#include <solis/dbg.h>

#define XHCI_MAX_CONTROLLERS 2
#define XHCI_MAX_SLOTS 32
#define XHCI_MAX_PORTS 255
#define XHCI_MAX_HUB_PORTS 15
#define XHCI_TRBS_PER_PAGE 256
#define XHCI_RING_USABLE 255
#define XHCI_MMIO_MAP_SIZE (256U * 1024U)

#define TRB_TYPE(value) ((uint32_t)(value) << 10)
#define TRB_CYCLE (1U << 0)
#define TRB_TOGGLE_CYCLE (1U << 1)
#define TRB_IOC (1U << 5)
#define TRB_IDT (1U << 6)
#define TRB_CHAIN (1U << 4)
#define TRB_DIR_IN (1U << 16)

#define PORTSC_CONNECTED (1U << 0)
#define PORTSC_ENABLED (1U << 1)
#define PORTSC_RESET (1U << 4)
#define PORTSC_POWER (1U << 9)
#define PORTSC_SPEED_MASK (0xFU << 10)
#define PORTSC_CHANGE_MASK (0x7FU << 17)
#define PORTSC_RESET_CHANGE (1U << 21)

#define USB_GET_DESCRIPTOR 6
#define USB_SET_CONFIGURATION 9
#define USB_SET_PROTOCOL 11
#define USB_CLASS_HUB 9
#define USB_CLASS_HID 3
#define USB_SUBCLASS_BOOT 1
#define USB_PROTOCOL_KEYBOARD 1
#define USB_PROTOCOL_MOUSE 2
#define USB_HID_REPORT_SIZE 8

struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct xhci_ring {
    struct xhci_trb *trbs;
    uint16_t enqueue;
    uint8_t cycle;
};

struct xhci_hid_device {
    bool active;
    bool keyboard;
    bool mouse;
    bool report_pending;
    uint8_t endpoint_id;
    uint16_t max_packet;
    uint8_t interface_number;
    struct xhci_ring ring;
    uint8_t *report;
    uintptr_t pending_trb;
};

struct xhci_device {
    bool active;
    bool hub;
    uint8_t speed;
    uint8_t root_port;
    uint8_t hub_slot;
    uint8_t hub_port;
    uint8_t depth;
    uint32_t route;
    uint8_t hub_port_count;
    uint8_t hub_power_delay;
    uint8_t child_slots[XHCI_MAX_HUB_PORTS + 1];
    bool child_attempted[XHCI_MAX_HUB_PORTS + 1];
    uint32_t last_hub_poll;
    uint8_t *device_context;
    uint8_t *input_context;
    uint8_t *control_buffer;
    struct xhci_ring ep0;
    struct xhci_hid_device hid;
};

struct xhci_controller {
    bool active;
    volatile uint8_t *mmio;
    volatile uint32_t *operational;
    uint32_t operational_offset;
    volatile uint32_t *doorbells;
    volatile uint32_t *runtime;
    uint32_t max_ports;
    uint32_t max_slots;
    uint32_t context_size;
    uint32_t max_packet_size;
    uint8_t controller_id;
    uint8_t *dcbaa;
    struct xhci_ring command_ring;
    volatile struct xhci_trb *event_ring;
    uint16_t event_dequeue;
    uint8_t event_cycle;
    struct xhci_device slots[XHCI_MAX_SLOTS + 1];
    uint8_t port_slots[XHCI_MAX_PORTS + 1];
    bool port_attempted[XHCI_MAX_PORTS + 1];
};

static struct xhci_controller controllers[XHCI_MAX_CONTROLLERS];
static uint32_t controller_count;

static void xhci_zero(void *memory, uint32_t size) {
    uint8_t *bytes = (uint8_t *)memory;
    for (uint32_t i = 0; i < size; i++) bytes[i] = 0;
}

static void *xhci_alloc_page(void) {
    void *page = pmm_alloc_page();
    if (page) xhci_zero(page, PMM_PAGE_SIZE);
    return page;
}

static void xhci_fence(void) {
    __asm__ volatile("mfence" : : : "memory");
}

static uint32_t xhci_read32(struct xhci_controller *controller,
                            uint32_t offset) {
    return *(volatile uint32_t *)(controller->mmio + offset);
}

static void xhci_write32(struct xhci_controller *controller,
                         uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(controller->mmio + offset) = value;
}

static void xhci_write64(struct xhci_controller *controller,
                         uint32_t offset, uint64_t value) {
    xhci_write32(controller, offset, (uint32_t)value);
    xhci_write32(controller, offset + 4, (uint32_t)(value >> 32));
}

static uint32_t xhci_reg_read(volatile uint32_t *base, uint32_t offset) {
    return base[offset / sizeof(uint32_t)];
}

static void xhci_reg_write(volatile uint32_t *base, uint32_t offset,
                           uint32_t value) {
    base[offset / sizeof(uint32_t)] = value;
}

static bool xhci_map_mmio(uint64_t physical, volatile uint8_t **mapped) {
    uint64_t start = physical & ~(uint64_t)(PMM_PAGE_SIZE - 1);
    uint64_t end = (physical + XHCI_MMIO_MAP_SIZE + PMM_PAGE_SIZE - 1) &
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

static bool xhci_get_bar(const struct pci_device_info *device,
                         uint64_t *base) {
    uint32_t low = pci_get_bar(device->bus, device->slot,
                               device->function, 0);
    if ((low & 1U) || low == 0) return false;
    uint32_t type = (low >> 1) & 3U;
    uint32_t high = 0;
    if (type == 2) {
        high = pci_get_bar(device->bus, device->slot,
                           device->function, 1);
    } else if (type != 0) {
        return false;
    }
    *base = ((uint64_t)high << 32) | (low & ~0xFULL);
    return *base != 0;
}

static void xhci_legacy_handoff(struct xhci_controller *controller,
                                uint32_t offset) {
    uint32_t max_offset = XHCI_MMIO_MAP_SIZE;
    for (uint32_t count = 0; offset && offset + 4 <= max_offset &&
                             count < 64; count++) {
        uint32_t header = xhci_read32(controller, offset);
        uint8_t id = (uint8_t)header;
        uint8_t next = (uint8_t)(header >> 8);
        if (id == 1) {
            uint32_t ownership = header | (1U << 24);
            xhci_write32(controller, offset, ownership);
            for (uint32_t wait = 0; wait < 1000000; wait++) {
                if (!(xhci_read32(controller, offset) & (1U << 16)))
                    break;
            }
            return;
        }
        if (!next) return;
        offset += (uint32_t)next * 4;
    }
}

static bool xhci_ring_init(struct xhci_ring *ring) {
    ring->trbs = (struct xhci_trb *)xhci_alloc_page();
    if (!ring->trbs) return false;
    ring->enqueue = 0;
    ring->cycle = 1;
    ring->trbs[XHCI_RING_USABLE].parameter =
        (uint64_t)(uintptr_t)ring->trbs;
    ring->trbs[XHCI_RING_USABLE].control =
        TRB_TYPE(6) | TRB_TOGGLE_CYCLE | TRB_CYCLE;
    xhci_fence();
    return true;
}

static struct xhci_trb *xhci_ring_push(struct xhci_ring *ring,
                                       uint64_t parameter,
                                       uint32_t status,
                                       uint32_t control) {
    if (ring->enqueue >= XHCI_RING_USABLE) {
        ring->trbs[XHCI_RING_USABLE].control =
            TRB_TYPE(6) | TRB_TOGGLE_CYCLE |
            (ring->cycle ? TRB_CYCLE : 0);
        xhci_fence();
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }
    struct xhci_trb *trb = &ring->trbs[ring->enqueue++];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = (control & ~TRB_CYCLE) |
                   (ring->cycle ? TRB_CYCLE : 0);
    xhci_fence();
    return trb;
}

static bool xhci_next_event(struct xhci_controller *controller,
                            struct xhci_trb *event) {
    volatile struct xhci_trb *current =
        &controller->event_ring[controller->event_dequeue];
    xhci_fence();
    if ((current->control & TRB_CYCLE) != controller->event_cycle)
        return false;
    event->parameter = current->parameter;
    event->status = current->status;
    event->control = current->control;
    controller->event_dequeue++;
    if (controller->event_dequeue == XHCI_TRBS_PER_PAGE) {
        controller->event_dequeue = 0;
        controller->event_cycle ^= 1;
    }
    xhci_write64(controller, (uint32_t)(controller->runtime -
                                        (volatile uint32_t *)controller->mmio) *
                                        sizeof(uint32_t) + 0x38,
                 (uint64_t)(uintptr_t)&controller->event_ring[
                     controller->event_dequeue] | (1U << 3));
    return true;
}

static bool xhci_wait_event(struct xhci_controller *controller,
                            uint8_t event_type, uintptr_t trb_address,
                            uint8_t *completion, uint8_t *slot_id,
                            uint32_t *residual) {
    struct xhci_trb event;
    uint8_t last_type = 0xFF;
    uint32_t last_parameter = 0;
    uint32_t start_tick = timer_get_ticks();
    for (uint32_t wait = 0; wait < 200000000; wait++) {
        while (xhci_next_event(controller, &event)) {
            uint8_t type = (event.control >> 10) & 0x3F;
            last_type = type;
            last_parameter = (uint32_t)event.parameter;
            if (type != event_type ||
                (trb_address && event.parameter != trb_address))
                continue;
            if (completion) *completion = (uint8_t)(event.status >> 24);
            if (slot_id) *slot_id = (uint8_t)(event.control >> 24);
            if (residual) *residual = event.status & 0xFFFFFF;
            return true;
        }
        if ((uint32_t)(timer_get_ticks() - start_tick) >= 200)
            break;
        __asm__ volatile("hlt");
    }
    if (event_type == 32) {
        dbg_print("[USB] Transfer event timeout; last type ");
        dbg_print_dec(last_type);
        dbg_print(" ptr ");
        dbg_print_hex(last_parameter);
        dbg_print(" index ");
        dbg_print_dec(controller->event_dequeue);
        dbg_print(" cycle ");
        dbg_print_dec(controller->event_cycle);
        dbg_print(" control ");
        dbg_print_hex(controller->event_ring[
            controller->event_dequeue].control);
        dbg_print("\n");
        for (uint32_t index = 0; index < 16; index++) {
            uint32_t control_value = controller->event_ring[index].control;
            if (control_value) {
                dbg_print("[USB] event ring ");
                dbg_print_dec(index);
                dbg_print(" control ");
                dbg_print_hex(control_value);
                dbg_print("\n");
            }
        }
    }
    return false;
}

static bool xhci_command(struct xhci_controller *controller,
                         uint64_t parameter, uint32_t status,
                         uint32_t control, uint8_t *slot_id) {
    struct xhci_trb *command =
        xhci_ring_push(&controller->command_ring, parameter, status,
                       control);
    uintptr_t command_address = (uintptr_t)command;
    xhci_write32(controller, (uint32_t)(controller->doorbells -
                                        (volatile uint32_t *)controller->mmio) *
                                        sizeof(uint32_t),
                 0);
    uint8_t completion = 0;
    if (!xhci_wait_event(controller, 33, command_address, &completion,
                         slot_id, 0) ||
        completion != 1)
        return false;
    return true;
}

static uint8_t *xhci_context(uint8_t *context, uint32_t context_size,
                             uint32_t index) {
    return context + (uint64_t)context_size * index;
}

static void xhci_copy_context(uint8_t *destination, const uint8_t *source,
                              uint32_t size) {
    for (uint32_t i = 0; i < size; i++) destination[i] = source[i];
}

static void xhci_slot_doorbell(struct xhci_controller *controller,
                               uint8_t slot, uint8_t target) {
    uint32_t doorbell_offset = (uint32_t)(controller->doorbells -
                                          (volatile uint32_t *)controller->mmio) *
                                          sizeof(uint32_t);
    xhci_write32(controller, doorbell_offset + (uint32_t)slot * 4, target);
}

static bool xhci_control(struct xhci_controller *controller,
                         struct xhci_device *device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index,
                         uint8_t *buffer, uint16_t length,
                         uint16_t *actual_length) {
    if (!device->active || length > PMM_PAGE_SIZE ||
        (length && !buffer))
        return false;

    uint64_t setup = (uint64_t)request_type |
                     ((uint64_t)request << 8) |
                     ((uint64_t)value << 16) |
                     ((uint64_t)index << 32) |
                     ((uint64_t)length << 48);
    uint32_t transfer_type = length && (request_type & 0x80) ? 3 : 0;
    (void)xhci_ring_push(&device->ep0, setup, 8,
                         TRB_TYPE(2) | TRB_IDT | TRB_CHAIN |
                             (transfer_type << 16));

    if (length) {
        if (buffer != device->control_buffer) {
            for (uint32_t i = 0; i < length; i++)
                device->control_buffer[i] = buffer[i];
        }
        (void)xhci_ring_push(&device->ep0,
                             (uint64_t)(uintptr_t)device->control_buffer,
                             length, TRB_TYPE(3) | TRB_CHAIN |
                                         ((request_type & 0x80) ? TRB_DIR_IN : 0));
    }

    (void)xhci_ring_push(&device->ep0, 0, 0,
                         TRB_TYPE(4) | TRB_IOC |
                             ((!length || !(request_type & 0x80))
                                  ? TRB_DIR_IN : 0));
    xhci_slot_doorbell(controller, device->hub_slot, 1);

    uint8_t completion = 0;
    uint32_t residual = 0;
    if (!xhci_wait_event(controller, 32, 0,
                         &completion, 0, &residual))
        return false;
    if (completion != 1 && completion != 13) {
        dbg_print("[USB] Control transfer completion code ");
        dbg_print_dec(completion);
        dbg_print("\n");
        return false;
    }

    if (length && request_type & 0x80) {
        uint32_t received = length - residual;
        if (received > length) return false;
        if (actual_length) *actual_length = (uint16_t)received;
        if (buffer != device->control_buffer) {
            for (uint32_t i = 0; i < received; i++)
                buffer[i] = device->control_buffer[i];
        }
    } else if (actual_length) {
        *actual_length = length;
    }
    return true;
}

static bool xhci_set_ep0_packet(struct xhci_controller *controller,
                                struct xhci_device *device,
                                uint16_t max_packet) {
    uint8_t *input = device->input_context;
    uint8_t *output = device->device_context;
    xhci_zero(input, PMM_PAGE_SIZE);
    xhci_copy_context(xhci_context(input, controller->context_size, 1),
                      xhci_context(output, controller->context_size, 0),
                      controller->context_size);
    xhci_copy_context(xhci_context(input, controller->context_size, 2),
                      xhci_context(output, controller->context_size, 1),
                      controller->context_size);
    uint32_t *control = (uint32_t *)input;
    control[1] = 1U << 1;
    uint32_t *ep0 = (uint32_t *)xhci_context(input,
                                             controller->context_size, 2);
    ep0[1] = (ep0[1] & 0x0000FFFFU) | ((uint32_t)max_packet << 16);

    uint8_t completion_slot = 0;
    if (!xhci_command(controller, (uint64_t)(uintptr_t)input, 0,
                      TRB_TYPE(13) | ((uint32_t)device->hub_slot << 24),
                      &completion_slot))
        return false;
    return completion_slot == device->hub_slot;
}

static bool xhci_address_device(struct xhci_controller *controller,
                                struct xhci_device *device,
                                uint8_t root_port, uint8_t speed,
                                uint32_t route, uint8_t depth,
                                uint8_t parent_hub_slot,
                                uint8_t parent_hub_port,
                                uint8_t parent_hub_speed) {
    device->device_context = (uint8_t *)xhci_alloc_page();
    device->input_context = (uint8_t *)xhci_alloc_page();
    device->control_buffer = (uint8_t *)xhci_alloc_page();
    if (!device->device_context || !device->input_context ||
        !device->control_buffer || !xhci_ring_init(&device->ep0))
        return false;

    controller->dcbaa[device->hub_slot * sizeof(uint64_t)] = 0;
    ((uint64_t *)controller->dcbaa)[device->hub_slot] =
        (uint64_t)(uintptr_t)device->device_context;

    xhci_zero(device->input_context, PMM_PAGE_SIZE);
    uint32_t *control = (uint32_t *)device->input_context;
    control[1] = 0x3;

    uint32_t *slot = (uint32_t *)xhci_context(
        device->input_context, controller->context_size, 1);
    slot[0] = (route & 0xFFFFF) | ((uint32_t)speed << 20) |
              (1U << 27);
    slot[1] = (uint32_t)root_port << 16;
    if (depth && parent_hub_slot &&
        parent_hub_speed == 3 && speed < 3) {
        slot[2] = parent_hub_slot |
                  ((uint32_t)parent_hub_port << 8);
    }

    uint32_t *ep0 = (uint32_t *)xhci_context(
        device->input_context, controller->context_size, 2);
    uint16_t max_packet = speed >= 4 ? 512 : 8;
    ep0[1] = (3U << 1) | (4U << 3) | ((uint32_t)max_packet << 16);
    ep0[2] = (uint32_t)(uintptr_t)device->ep0.trbs | 1U;
    ep0[3] = (uint32_t)((uint64_t)(uintptr_t)device->ep0.trbs >> 32);
    ep0[4] = 8U;
    xhci_fence();

    uint8_t completion_slot = 0;
    if (!xhci_command(controller,
                      (uint64_t)(uintptr_t)device->input_context, 0,
                      TRB_TYPE(11) | ((uint32_t)device->hub_slot << 24),
                      &completion_slot) ||
        completion_slot != device->hub_slot)
        return false;
    device->root_port = root_port;
    device->speed = speed;
    device->route = route;
    device->depth = depth;
    device->hub_port = parent_hub_port;
    return true;
}

static bool xhci_descriptor(struct xhci_controller *controller,
                            struct xhci_device *device, uint8_t type,
                            uint8_t descriptor_index, uint8_t *buffer,
                            uint16_t length, uint16_t *actual_length) {
    return xhci_control(controller, device, 0x80, USB_GET_DESCRIPTOR,
                        ((uint16_t)type << 8) | descriptor_index, 0,
                        buffer, length, actual_length);
}

static bool xhci_read_hub_descriptor(struct xhci_controller *controller,
                                     struct xhci_device *device) {
    uint8_t descriptor[8] = {0};
    uint16_t actual = 0;
    if (!xhci_control(controller, device, 0xA0, USB_GET_DESCRIPTOR,
                      0x2900, 0, descriptor, 7, &actual) ||
        actual < 7 || descriptor[2] == 0 ||
        descriptor[2] > XHCI_MAX_HUB_PORTS)
        return false;

    device->hub = true;
    device->hub_port_count = descriptor[2];
    device->hub_power_delay = descriptor[5];
    xhci_zero(device->input_context, PMM_PAGE_SIZE);
    xhci_copy_context(xhci_context(device->input_context,
                                   controller->context_size, 1),
                      xhci_context(device->device_context,
                                   controller->context_size, 0),
                      controller->context_size);
    uint32_t *slot_context = (uint32_t *)xhci_context(
        device->input_context, controller->context_size, 1);
    slot_context[0] |= 1U << 26;
    slot_context[1] = (slot_context[1] & 0x00FFFFFFU) |
                      ((uint32_t)device->hub_port_count << 24);
    ((uint32_t *)device->input_context)[1] = 1U;
    uint8_t completion_slot = 0;
    if (!xhci_command(controller,
                      (uint64_t)(uintptr_t)device->input_context, 0,
                      TRB_TYPE(13) | ((uint32_t)device->hub_slot << 24),
                      &completion_slot) ||
        completion_slot != device->hub_slot)
        return false;

    for (uint8_t port = 1; port <= device->hub_port_count; port++) {
        if (!xhci_control(controller, device, 0x23, 3, 8, port,
                          0, 0, 0))
            return false;
    }
    return true;
}

static bool xhci_configure_endpoint(struct xhci_controller *controller,
                                    struct xhci_device *device,
                                    uint8_t endpoint_address,
                                    uint8_t interval,
                                    uint16_t max_packet) {
    uint8_t endpoint_number = endpoint_address & 0x0F;
    uint8_t endpoint_id = (uint8_t)(endpoint_number * 2 +
                         ((endpoint_address & 0x80) ? 1 : 0));
    if (endpoint_id < 2 || endpoint_id > 31 || max_packet == 0 ||
        max_packet > 1024)
        return false;

    struct xhci_ring ring;
    if (!xhci_ring_init(&ring)) return false;
    device->hid.ring = ring;
    device->hid.report = (uint8_t *)xhci_alloc_page();
    if (!device->hid.report) return false;

    uint8_t *input = device->input_context;
    uint8_t *output = device->device_context;
    xhci_zero(input, PMM_PAGE_SIZE);
    xhci_copy_context(xhci_context(input, controller->context_size, 1),
                      xhci_context(output, controller->context_size, 0),
                      controller->context_size);
    xhci_copy_context(xhci_context(input, controller->context_size, 2),
                      xhci_context(output, controller->context_size, 1),
                      controller->context_size);

    uint32_t *slot = (uint32_t *)xhci_context(
        input, controller->context_size, 1);
    slot[0] = (slot[0] & ~(0x1FU << 27)) |
              ((uint32_t)endpoint_id << 27);

    uint32_t context_index = (uint32_t)endpoint_id + 1;
    uint32_t *endpoint = (uint32_t *)xhci_context(
        input, controller->context_size, context_index);
    uint32_t interval_value = 0;
    if (device->speed >= 3) {
        interval_value = interval ? interval - 1 : 0;
    } else {
        uint32_t microframes = (uint32_t)(interval ? interval : 1) * 8;
        interval_value = 0;
        while ((1U << interval_value) < microframes &&
               interval_value < 15)
            interval_value++;
    }
    endpoint[0] = (interval_value & 0xFF) << 16;
    endpoint[1] = (3U << 1) | (7U << 3) |
                  ((uint32_t)max_packet << 16);
    endpoint[2] = (uint32_t)(uintptr_t)device->hid.ring.trbs | 1U;
    endpoint[3] =
        (uint32_t)((uint64_t)(uintptr_t)device->hid.ring.trbs >> 32);
    endpoint[4] = 8U | ((uint32_t)max_packet << 16);
    ((uint32_t *)input)[1] = 1U | (1U << endpoint_id);
    xhci_fence();

    uint8_t completion_slot = 0;
    if (!xhci_command(controller, (uint64_t)(uintptr_t)input, 0,
                      TRB_TYPE(12) | ((uint32_t)device->hub_slot << 24),
                      &completion_slot) ||
        completion_slot != device->hub_slot)
        return false;

    device->hid.endpoint_id = endpoint_id;
    device->hid.max_packet = max_packet;
    device->hid.active = true;
    return true;
}

static bool xhci_find_boot_hid(const uint8_t *configuration,
                               uint16_t length, uint8_t *interface_number,
                               uint8_t *protocol, uint8_t *endpoint_address,
                               uint8_t *interval, uint16_t *max_packet) {
    bool boot_interface = false;
    uint8_t current_interface = 0;
    uint8_t current_protocol = 0;
    for (uint16_t offset = 0; offset + 2 <= length;) {
        uint8_t descriptor_length = configuration[offset];
        uint8_t descriptor_type = configuration[offset + 1];
        if (descriptor_length < 2 || descriptor_length > length - offset)
            return false;
        if (descriptor_type == 4 && descriptor_length >= 9) {
            current_interface = configuration[offset + 2];
            boot_interface = configuration[offset + 5] == USB_CLASS_HID &&
                             configuration[offset + 6] == USB_SUBCLASS_BOOT;
            current_protocol = configuration[offset + 7];
        } else if (descriptor_type == 5 && descriptor_length >= 7 &&
                   boot_interface &&
                   (configuration[offset + 2] & 0x80) &&
                   (configuration[offset + 3] & 3) == 3) {
            uint16_t packet = (uint16_t)configuration[offset + 4] |
                              ((uint16_t)configuration[offset + 5] << 8);
            packet &= 0x07FF;
            if (packet < 3 || packet > 1024) return false;
            *interface_number = current_interface;
            *protocol = current_protocol;
            *endpoint_address = configuration[offset + 2];
            *interval = configuration[offset + 6];
            *max_packet = packet;
            return current_protocol == USB_PROTOCOL_KEYBOARD ||
                   current_protocol == USB_PROTOCOL_MOUSE;
        }
        offset += descriptor_length;
    }
    return false;
}

static bool xhci_configure_hid(struct xhci_controller *controller,
                               struct xhci_device *device,
                               const uint8_t *configuration,
                               uint16_t configuration_length) {
    uint8_t interface_number = 0;
    uint8_t protocol = 0;
    uint8_t endpoint_address = 0;
    uint8_t interval = 0;
    uint16_t max_packet = 0;
    if (!xhci_find_boot_hid(configuration, configuration_length,
                            &interface_number, &protocol,
                            &endpoint_address, &interval, &max_packet))
        return false;
    if (!xhci_control(controller, device, 0x00, USB_SET_CONFIGURATION,
                      configuration[5], 0, 0, 0, 0) ||
        !xhci_control(controller, device, 0x21, USB_SET_PROTOCOL, 0,
                      interface_number, 0, 0, 0))
        return false;

    if (!xhci_configure_endpoint(controller, device, endpoint_address,
                                 interval, max_packet))
        return false;
    device->hid.interface_number = interface_number;
    device->hid.keyboard = protocol == USB_PROTOCOL_KEYBOARD;
    device->hid.mouse = protocol == USB_PROTOCOL_MOUSE;
    dbg_print("[USB] Boot HID ");
    dbg_print(device->hid.keyboard ? "keyboard" : "mouse");
    dbg_print(" active on slot ");
    dbg_print_dec(device->hub_slot);
    dbg_print("\n");
    return true;
}

static bool xhci_enumerate_device(struct xhci_controller *controller,
                                  uint8_t root_port, uint8_t speed,
                                  uint32_t route, uint8_t depth,
                                  uint8_t parent_hub_slot,
                                  uint8_t parent_hub_port,
                                  uint8_t parent_hub_speed) {
    if (depth > 5 || controller->max_slots == 0) return false;
    uint8_t slot_id = 0;
    if (!xhci_command(controller, 0, 0, TRB_TYPE(9), &slot_id) ||
        slot_id == 0 || slot_id > XHCI_MAX_SLOTS)
        return false;

    struct xhci_device *device = &controller->slots[slot_id];
    xhci_zero(device, sizeof(*device));
    device->active = true;
    device->hub_slot = slot_id;
    if (!xhci_address_device(controller, device, root_port, speed, route,
                             depth, parent_hub_slot, parent_hub_port,
                             parent_hub_speed)) {
        dbg_print("[USB] Address Device failed for port ");
        dbg_print_dec(root_port);
        dbg_print("\n");
        return false;
    }

    uint8_t descriptor[18] = {0};
    uint16_t actual = 0;
    if (!xhci_descriptor(controller, device, 1, 0, descriptor, 8, &actual) ||
        actual < 8) {
        dbg_print("[USB] Device descriptor request failed on slot ");
        dbg_print_dec(slot_id);
        dbg_print("\n");
        return false;
    }

    uint16_t max_packet = descriptor[7];
    if (speed >= 4) {
        if (descriptor[7] != 9) return false;
        max_packet = 512;
    }
    if (!max_packet || !xhci_set_ep0_packet(controller, device, max_packet))
        return false;
    if (!xhci_descriptor(controller, device, 1, 0, descriptor,
                         sizeof(descriptor), &actual) || actual < 18) {
        dbg_print("[USB] Full device descriptor request failed on slot ");
        dbg_print_dec(slot_id);
        dbg_print("\n");
        return false;
    }

    uint8_t config_header[9] = {0};
    if (!xhci_descriptor(controller, device, 2, 0, config_header,
                         sizeof(config_header), &actual) || actual < 9)
        return false;
    uint16_t total_length = (uint16_t)config_header[2] |
                            ((uint16_t)config_header[3] << 8);
    if (total_length < 9 || total_length > PMM_PAGE_SIZE) return false;
    uint8_t *configuration = device->control_buffer;
    if (!xhci_descriptor(controller, device, 2, 0, configuration,
                         total_length, &actual) || actual < total_length)
        return false;

    if (descriptor[4] == 9) {
        uint8_t config_value = configuration[5];
        if (!xhci_control(controller, device, 0, USB_SET_CONFIGURATION,
                          config_value, 0, 0, 0, 0) ||
            !xhci_read_hub_descriptor(controller, device)) {
            dbg_print("[USB] Hub initialization failed on slot ");
            dbg_print_dec(slot_id);
            dbg_print("\n");
            return false;
        }
        dbg_print("[USB] Hub active on slot ");
        dbg_print_dec(slot_id);
        dbg_print(", ports ");
        dbg_print_dec(device->hub_port_count);
        dbg_print("\n");
        return true;
    }

    if (descriptor[4] != 0 && descriptor[4] != USB_CLASS_HID)
        return false;
    if (!xhci_configure_hid(controller, device, configuration,
                            total_length)) {
        dbg_print("[USB] Unsupported device on slot ");
        dbg_print_dec(slot_id);
        dbg_print("\n");
        return false;
    }
    return true;
}

static void xhci_clear_port_changes(struct xhci_controller *controller,
                                    uint8_t port) {
    uint32_t offset = controller->operational_offset + 0x400 +
                      ((uint32_t)port - 1) * 0x10;
    uint32_t value = xhci_read32(controller, offset);
    uint32_t changes = value & PORTSC_CHANGE_MASK;
    if (changes) xhci_write32(controller, offset, changes);
}

static bool xhci_reset_root_port(struct xhci_controller *controller,
                                 uint8_t port, uint8_t *speed) {
    uint32_t offset = controller->operational_offset + 0x400 +
                      ((uint32_t)port - 1) * 0x10;
    uint32_t portsc = xhci_read32(controller, offset);
    if (!(portsc & PORTSC_CONNECTED)) return false;
    uint32_t speed_id = (portsc & PORTSC_SPEED_MASK) >> 10;
    if (!speed_id || speed_id > 5) return false;

    xhci_write32(controller, offset,
                 PORTSC_RESET | (portsc & PORTSC_POWER));
    bool reset_complete = false;
    for (uint32_t wait = 0; wait < 10000000; wait++) {
        portsc = xhci_read32(controller, offset);
        if (!(portsc & PORTSC_RESET) &&
            (portsc & PORTSC_RESET_CHANGE)) {
            reset_complete = true;
            break;
        }
    }
    xhci_clear_port_changes(controller, port);
    if (!reset_complete || !(portsc & PORTSC_CONNECTED) ||
        !(portsc & PORTSC_ENABLED))
        return false;
    *speed = (uint8_t)((portsc & PORTSC_SPEED_MASK) >> 10);
    return true;
}

static void xhci_scan_root_ports(struct xhci_controller *controller) {
    for (uint32_t port = 1; port <= controller->max_ports; port++) {
        uint32_t offset = controller->operational_offset + 0x400 +
                          (port - 1) * 0x10;
        uint32_t portsc = xhci_read32(controller, offset);
        if (!(portsc & PORTSC_CONNECTED)) {
            controller->port_attempted[port] = false;
            uint8_t disconnected_slot = controller->port_slots[port];
            if (disconnected_slot) {
                (void)xhci_command(controller, 0, 0,
                                   TRB_TYPE(10) |
                                       ((uint32_t)disconnected_slot << 24),
                                   0);
                xhci_zero(&controller->slots[disconnected_slot],
                          sizeof(controller->slots[disconnected_slot]));
                ((uint64_t *)controller->dcbaa)[disconnected_slot] = 0;
                controller->port_slots[port] = 0;
            }
            continue;
        }
        if (controller->port_attempted[port]) continue;
        controller->port_attempted[port] = true;
        uint8_t speed = 0;
        if (!xhci_reset_root_port(controller, (uint8_t)port, &speed)) {
            dbg_print("[USB] Could not reset xHCI root port ");
            dbg_print_dec(port);
            dbg_print("\n");
            continue;
        }
        uint8_t slot_id = 0;
        if (xhci_enumerate_device(controller, (uint8_t)port, speed, 0, 0,
                                  0, 0, 0)) {
            for (uint8_t slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
                if (controller->slots[slot].active &&
                    controller->slots[slot].root_port == port &&
                    controller->slots[slot].depth == 0) {
                    slot_id = slot;
                    break;
                }
            }
        }
        controller->port_slots[port] = slot_id;
    }
}

static bool xhci_hub_port_status(struct xhci_controller *controller,
                                 struct xhci_device *hub, uint8_t port,
                                 uint16_t *status, uint16_t *change) {
    uint8_t data[4] = {0};
    uint16_t actual = 0;
    if (!xhci_control(controller, hub, 0xA3, 0, 0, port,
                      data, sizeof(data), &actual) || actual < 4)
        return false;
    *status = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    *change = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
    return true;
}

static bool xhci_reset_hub_port(struct xhci_controller *controller,
                                struct xhci_device *hub, uint8_t port,
                                uint8_t *speed) {
    if (!xhci_control(controller, hub, 0x23, 3, 4, port, 0, 0, 0))
        return false;
    uint16_t status = 0, change = 0;
    bool complete = false;
    for (uint32_t wait = 0; wait < 2000000; wait++) {
        if (!xhci_hub_port_status(controller, hub, port, &status, &change))
            return false;
        if (!(status & (1U << 4)) && (change & (1U << 4))) {
            complete = true;
            break;
        }
    }
    if (!complete || !(status & 1)) return false;

    if (change) {
        static const uint8_t features[] = {16, 17, 18, 19, 20, 21, 22, 23};
        for (uint32_t bit = 0; bit < 8; bit++)
            if (change & (1U << bit))
                (void)xhci_control(controller, hub, 0x23, 1,
                                   features[bit], port, 0, 0, 0);
    }
    *speed = (status & (1U << 9)) ? 2 :
             (status & (1U << 10)) ? 3 : 1;
    return true;
}

static void xhci_poll_hubs(struct xhci_controller *controller) {
    uint32_t now = timer_get_ticks();
    for (uint8_t slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        struct xhci_device *hub = &controller->slots[slot];
        if (!hub->active || !hub->hub ||
            now - hub->last_hub_poll < 10)
            continue;
        hub->last_hub_poll = now;
        for (uint8_t port = 1; port <= hub->hub_port_count; port++) {
            uint16_t status = 0, change = 0;
            if (!xhci_hub_port_status(controller, hub, port,
                                      &status, &change))
                continue;
            if (!(status & 1)) {
                hub->child_attempted[port] = false;
                uint8_t child_slot = hub->child_slots[port];
                if (child_slot) {
                    (void)xhci_command(controller, 0, 0,
                                       TRB_TYPE(10) |
                                           ((uint32_t)child_slot << 24),
                                       0);
                    xhci_zero(&controller->slots[child_slot],
                              sizeof(controller->slots[child_slot]));
                    ((uint64_t *)controller->dcbaa)[child_slot] = 0;
                    hub->child_slots[port] = 0;
                }
                continue;
            }
            if (hub->child_attempted[port]) continue;
            hub->child_attempted[port] = true;
            uint8_t speed = 0;
            if (!xhci_reset_hub_port(controller, hub, port, &speed)) {
                dbg_print("[USB] Could not reset hub port ");
                dbg_print_dec(port);
                dbg_print("\n");
                continue;
            }
            uint8_t depth = hub->depth + 1;
            uint32_t route = hub->route |
                             ((uint32_t)port << (4 * hub->depth));
            if (xhci_enumerate_device(controller, hub->root_port, speed,
                                      route, depth, slot, port,
                                      hub->speed)) {
                for (uint8_t child = 1; child <= XHCI_MAX_SLOTS; child++) {
                    struct xhci_device *device =
                        &controller->slots[child];
                    if (device->active && device->depth == depth &&
                        device->root_port == hub->root_port &&
                        device->route == route) {
                        hub->child_slots[port] = child;
                        break;
                    }
                }
            }
        }
    }
}

static void xhci_queue_hid_report(struct xhci_controller *controller,
                                  uint8_t slot,
                                  struct xhci_device *device) {
    struct xhci_hid_device *hid = &device->hid;
    if (!hid->active || hid->report_pending) return;
    xhci_zero(hid->report, PMM_PAGE_SIZE);
    struct xhci_trb *trb =
        xhci_ring_push(&hid->ring,
                       (uint64_t)(uintptr_t)hid->report,
                       hid->max_packet, TRB_TYPE(1) | TRB_IOC);
    hid->pending_trb = (uintptr_t)trb;
    hid->report_pending = true;
    xhci_slot_doorbell(controller, slot, hid->endpoint_id);
}

static void xhci_process_events(struct xhci_controller *controller) {
    struct xhci_trb event;
    while (xhci_next_event(controller, &event)) {
        uint8_t type = (event.control >> 10) & 0x3F;
        if (type != 32) continue;
        uint8_t slot = (uint8_t)(event.control >> 24);
        uint8_t endpoint = (event.control >> 16) & 0x1F;
        if (!slot || slot > XHCI_MAX_SLOTS) continue;
        struct xhci_device *device = &controller->slots[slot];
        struct xhci_hid_device *hid = &device->hid;
        if (!device->active || !hid->active || !hid->report_pending ||
            endpoint != hid->endpoint_id ||
            event.parameter != hid->pending_trb)
            continue;

        uint8_t completion = (uint8_t)(event.status >> 24);
        uint32_t residual = event.status & 0xFFFFFF;
        uint32_t received = residual <= hid->max_packet
                                ? hid->max_packet - residual : 0;
        if ((completion == 1 || completion == 13) &&
            hid->keyboard && received >= USB_HID_REPORT_SIZE) {
            keyboard_submit_hid_report(hid->report[0],
                                       &hid->report[2]);
        } else if ((completion == 1 || completion == 13) &&
                   hid->mouse && received >= 3) {
            mouse_submit_usb_report(hid->report[0],
                                    (int8_t)hid->report[1],
                                    (int8_t)hid->report[2]);
        } else if (completion != 1 && completion != 13) {
            dbg_print("[USB] HID transfer failed, code ");
            dbg_print_dec(completion);
            dbg_print("\n");
        }
        hid->report_pending = false;
    }
}

static bool xhci_controller_init(struct xhci_controller *controller,
                                 const struct pci_device_info *device,
                                 uint8_t controller_id) {
    uint64_t bar = 0;
    if (!xhci_get_bar(device, &bar) ||
        !pci_enable_command_bits(device,
                                 PCI_COMMAND_IO_SPACE |
                                     PCI_COMMAND_BUS_MASTER))
        return false;
    uint16_t command = (uint16_t)pci_read_config(
        device->bus, device->slot, device->function, 0x04);
    command |= 0x0002;
    pci_write_config(device->bus, device->slot, device->function, 0x04,
                     command);

    if (!xhci_map_mmio(bar, &controller->mmio)) return false;
    uint8_t cap_length = controller->mmio[0];
    uint32_t hcsparams1 = xhci_read32(controller, 0x04);
    uint32_t hccparams1 = xhci_read32(controller, 0x10);
    uint32_t doorbell_offset = xhci_read32(controller, 0x14) & ~3U;
    uint32_t runtime_offset = xhci_read32(controller, 0x18) & ~0x1FU;
    controller->max_slots = hcsparams1 & 0xFF;
    controller->max_ports = (hcsparams1 >> 24) & 0xFF;
    controller->context_size = (hccparams1 & (1U << 2)) ? 64 : 32;
    controller->controller_id = controller_id;
    if (cap_length < 0x20 || !controller->max_slots ||
        !controller->max_ports || controller->max_ports > XHCI_MAX_PORTS ||
        doorbell_offset >= XHCI_MMIO_MAP_SIZE ||
        runtime_offset + 0x40 >= XHCI_MMIO_MAP_SIZE)
        return false;

    controller->operational_offset = cap_length;
    controller->operational =
        (volatile uint32_t *)(controller->mmio + cap_length);
    controller->doorbells =
        (volatile uint32_t *)(controller->mmio + doorbell_offset);
    controller->runtime =
        (volatile uint32_t *)(controller->mmio + runtime_offset);
    xhci_legacy_handoff(controller, (hccparams1 >> 16) * 4);

    uint32_t command_reg = xhci_reg_read(controller->operational, 0);
    xhci_reg_write(controller->operational, 0, command_reg & ~1U);
    uint32_t wait = 0;
    for (; wait < 10000000; wait++)
        if (xhci_reg_read(controller->operational, 4) & 1U) break;
    if (wait == 10000000) return false;

    xhci_reg_write(controller->operational, 0,
                   xhci_reg_read(controller->operational, 0) | 2U);
    for (wait = 0; wait < 10000000; wait++)
        if (!(xhci_reg_read(controller->operational, 0) & 2U) &&
            !(xhci_reg_read(controller->operational, 4) & (1U << 11)))
            break;
    if (wait == 10000000) return false;

    uint32_t page_size = xhci_reg_read(controller->operational, 8);
    if (!(page_size & 1U)) return false;

    controller->dcbaa = (uint8_t *)xhci_alloc_page();
    controller->event_ring = (struct xhci_trb *)xhci_alloc_page();
    struct xhci_trb *erst = (struct xhci_trb *)xhci_alloc_page();
    if (!controller->dcbaa || !controller->event_ring || !erst ||
        !xhci_ring_init(&controller->command_ring))
        return false;
    controller->event_dequeue = 0;
    controller->event_cycle = 1;
    erst[0].parameter = (uint64_t)(uintptr_t)controller->event_ring;
    erst[0].status = XHCI_TRBS_PER_PAGE;

    uint32_t max_slots = controller->max_slots;
    if (max_slots > XHCI_MAX_SLOTS) max_slots = XHCI_MAX_SLOTS;
    xhci_reg_write(controller->operational, 0x30,
                   (uint32_t)(uintptr_t)controller->dcbaa);
    xhci_reg_write(controller->operational, 0x34,
                   (uint32_t)((uint64_t)(uintptr_t)controller->dcbaa >> 32));
    xhci_reg_write(controller->operational, 0x38, max_slots);

    uint32_t runtime_offset_value =
        (uint32_t)(controller->runtime -
                   (volatile uint32_t *)controller->mmio) *
                   sizeof(uint32_t);
    xhci_write32(controller, runtime_offset_value + 0x20, 0);
    xhci_write32(controller, runtime_offset_value + 0x28, 1);
    xhci_write64(controller, runtime_offset_value + 0x30,
                 (uint64_t)(uintptr_t)erst);
    xhci_write64(controller, runtime_offset_value + 0x38,
                 (uint64_t)(uintptr_t)controller->event_ring);

    uint64_t command_ring_address =
        (uint64_t)(uintptr_t)controller->command_ring.trbs | 1U;
    xhci_reg_write(controller->operational, 0x18,
                   (uint32_t)command_ring_address);
    xhci_reg_write(controller->operational, 0x1C,
                   (uint32_t)(command_ring_address >> 32));

    command_reg = xhci_reg_read(controller->operational, 0);
    command_reg &= ~((1U << 2) | (1U << 3));
    command_reg |= 1U;
    xhci_reg_write(controller->operational, 0, command_reg);
    for (wait = 0; wait < 10000000; wait++)
        if (!(xhci_reg_read(controller->operational, 4) & 1U)) break;
    if (wait == 10000000) return false;

    controller->active = true;
    dbg_print("[USB] xHCI ");
    dbg_print_dec(controller_id);
    dbg_print(" active, ports ");
    dbg_print_dec(controller->max_ports);
    dbg_print("\n");
    return true;
}

static bool xhci_same_device(const struct pci_device_info *a,
                             const struct pci_device_info *b) {
    return a->bus == b->bus && a->slot == b->slot &&
           a->function == b->function;
}

void usb_init(void) {
    controller_count = 0;
    dbg_set_driver_state("usb", DBG_DRIVER_UNINITIALIZED);
    struct pci_device_info candidates[XHCI_MAX_CONTROLLERS];
    uint32_t count = 0;
    static const uint16_t amd_xhci_ids[] = {0x15BA, 0x43ED};
    for (uint32_t i = 0; i < 2; i++) {
        struct pci_device_info found;
        if (pci_find_device(0x1022, amd_xhci_ids[i], &found))
            candidates[count++] = found;
    }
    if (!count) {
        struct pci_device_info found;
        if (pci_find_class(0x0C, 0x03, 0x30, &found))
            candidates[count++] = found;
    }

    for (uint32_t i = 0; i < count; i++) {
        bool duplicate = false;
        for (uint32_t j = 0; j < i; j++)
            if (xhci_same_device(&candidates[i], &candidates[j]))
                duplicate = true;
        if (duplicate) continue;
        if (xhci_controller_init(&controllers[controller_count],
                                 &candidates[i],
                                 (uint8_t)controller_count)) {
            controller_count++;
            if (controller_count == XHCI_MAX_CONTROLLERS) break;
        } else {
            dbg_print("[USB] xHCI controller initialization failed\n");
        }
    }

    if (!controller_count) {
        dbg_print("[USB] No working xHCI host controller found\n");
        dbg_set_driver_state("usb", DBG_DRIVER_FAILED);
        return;
    }
    dbg_set_driver_state("usb", DBG_DRIVER_ACTIVE);
    for (uint32_t i = 0; i < controller_count; i++)
        xhci_scan_root_ports(&controllers[i]);
}

void usb_poll(void) {
    for (uint32_t i = 0; i < controller_count; i++) {
        struct xhci_controller *controller = &controllers[i];
        xhci_process_events(controller);
        xhci_scan_root_ports(controller);
        xhci_poll_hubs(controller);
        for (uint8_t slot = 1; slot <= XHCI_MAX_SLOTS; slot++)
            if (controller->slots[slot].active)
                xhci_queue_hid_report(controller, slot,
                                      &controller->slots[slot]);
    }
}

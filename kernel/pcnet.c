#include <solis/pcnet.h>
#include <solis/ports.h>
#include <solis/dbg.h>
#include <stdbool.h>

/*
 * Polling driver for the AMD PCnet-FAST III (Am79C973) and compatible
 * PCnet-PCI controllers. VirtualBox exposes this adapter as "PCnet-FAST III"
 * and uses it for its NAT network mode.
 *
 * The controller is put into 32-bit software style (SWSTYLE=2, SSIZE32=1) so
 * its initialization block and descriptor rings use the 16-byte layouts below.
 * Interrupts stay masked; the kernel network stack polls receive frames.
 */

#define PCNET_RX_DESC_COUNT 32
#define PCNET_TX_DESC_COUNT 8
#define PCNET_BUFFER_SIZE   1536

#define PCNET_RX_LOG2 5
#define PCNET_TX_LOG2 3

#define PCNET_IO_RDP     0x10
#define PCNET_IO_RAP     0x14
#define PCNET_IO_RESET   0x18
#define PCNET_IO_BDP     0x1C

#define PCNET_CSR0 0
#define PCNET_CSR1 1
#define PCNET_CSR2 2
#define PCNET_CSR3 3
#define PCNET_CSR4 4

#define PCNET_CSR0_INIT  0x0001
#define PCNET_CSR0_STRT  0x0002
#define PCNET_CSR0_STOP  0x0004
#define PCNET_CSR0_TDMD  0x0008
#define PCNET_CSR0_IDON  0x0100
#define PCNET_CSR0_RXON  0x0020
#define PCNET_CSR0_TXON  0x0010

#define PCNET_BCR_SWS 20

#define PCNET_RMD_OWN 0x8000
#define PCNET_RMD_ERR 0x4000
#define PCNET_RMD_STP 0x0200
#define PCNET_RMD_ENP 0x0100

#define PCNET_TMD_OWN 0x8000
#define PCNET_TMD_STP 0x0200
#define PCNET_TMD_ENP 0x0100

struct pcnet_init_block {
    uint16_t mode;
    uint16_t tlen_rlen;
    uint8_t  phys_addr[6];
    uint16_t reserved;
    uint32_t filter[2];
    uint32_t rx_ring;
    uint32_t tx_ring;
} __attribute__((packed));

struct pcnet_rx_desc {
    uint32_t base;
    uint16_t buf_length;
    uint16_t status;
    uint32_t msg_length;
    uint32_t reserved;
} __attribute__((packed));

struct pcnet_tx_desc {
    uint32_t base;
    uint16_t length;
    uint16_t status;
    uint32_t misc;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct pcnet_init_block) == 28,
               "PCnet 32-bit init block must be 28 bytes");
_Static_assert(sizeof(struct pcnet_rx_desc) == 16,
               "PCnet 32-bit RX descriptor must be 16 bytes");
_Static_assert(sizeof(struct pcnet_tx_desc) == 16,
               "PCnet 32-bit TX descriptor must be 16 bytes");

static uint16_t io_base;
static bool initialized;
static uint8_t mac_address[6];

static struct pcnet_init_block init_block __attribute__((aligned(16)));
static struct pcnet_rx_desc rx_ring[PCNET_RX_DESC_COUNT] __attribute__((aligned(16)));
static struct pcnet_tx_desc tx_ring[PCNET_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t rx_buffers[PCNET_RX_DESC_COUNT][PCNET_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffers[PCNET_TX_DESC_COUNT][PCNET_BUFFER_SIZE] __attribute__((aligned(16)));
static uint32_t rx_index;
static uint32_t tx_index;

static void pcnet_barrier(void) {
    __asm__ volatile("" ::: "memory");
}

static bool pcnet_dma_address(const void *buffer, uint32_t size, uint32_t *address) {
    uintptr_t start = (uintptr_t)buffer;
    if (!buffer || !address || size == 0 || start > UINT32_MAX ||
        (uintptr_t)(size - 1) > UINT32_MAX - start)
        return false;
    *address = (uint32_t)start;
    return true;
}

static void pcnet_write_csr(uint16_t reg, uint16_t value) {
    outl(io_base + PCNET_IO_RAP, reg);
    outl(io_base + PCNET_IO_RDP, value);
}

static uint16_t pcnet_read_csr(uint16_t reg) {
    outl(io_base + PCNET_IO_RAP, reg);
    return (uint16_t)inl(io_base + PCNET_IO_RDP);
}

static void pcnet_write_bcr(uint16_t reg, uint16_t value) {
    outl(io_base + PCNET_IO_RAP, reg);
    outl(io_base + PCNET_IO_BDP, value);
}

static uint16_t pcnet_read_bcr(uint16_t reg) {
    outl(io_base + PCNET_IO_RAP, reg);
    return (uint16_t)inl(io_base + PCNET_IO_BDP);
}

static uint16_t pcnet_rx_status(uint32_t index) {
    return *(volatile uint16_t *)&rx_ring[index].status;
}

static uint16_t pcnet_tx_status(uint32_t index) {
    return *(volatile uint16_t *)&tx_ring[index].status;
}

static void pcnet_reset(void) {
    (void)inl(io_base + PCNET_IO_RESET);
    (void)inw(io_base + PCNET_IO_RAP);
    for (int i = 0; i < 10; i++)
        io_wait();
}

static bool pcnet_read_mac(void) {
    bool any_set = false;
    bool all_ones = true;

    for (int i = 0; i < 6; i++) {
        mac_address[i] = inb(io_base + (uint16_t)i);
        if (mac_address[i])
            any_set = true;
        if (mac_address[i] != 0xFF)
            all_ones = false;
    }
    return any_set && !all_ones;
}

static void pcnet_log_mac(void) {
    dbg_print("[PCNET] MAC=");
    for (int i = 0; i < 6; i++) {
        dbg_print_hex(mac_address[i]);
        if (i < 5)
            dbg_print(":");
    }
    dbg_print("\n");
}

static bool pcnet_setup_rings(void) {
    uint32_t rx_ring_address;
    uint32_t tx_ring_address;

    if (!pcnet_dma_address(rx_ring, sizeof(rx_ring), &rx_ring_address) ||
        !pcnet_dma_address(tx_ring, sizeof(tx_ring), &tx_ring_address)) {
        dbg_print("[PCNET] Descriptor rings exceed 32-bit DMA address range\n");
        return false;
    }

    for (uint32_t i = 0; i < PCNET_RX_DESC_COUNT; i++) {
        uint32_t buffer_address;
        if (!pcnet_dma_address(rx_buffers[i], PCNET_BUFFER_SIZE,
                               &buffer_address)) {
            dbg_print("[PCNET] RX buffer exceeds 32-bit DMA address range\n");
            return false;
        }
        rx_ring[i].base = buffer_address;
        rx_ring[i].buf_length = (uint16_t)(-PCNET_BUFFER_SIZE);
        rx_ring[i].msg_length = 0;
        rx_ring[i].reserved = 0;
        pcnet_barrier();
        rx_ring[i].status = PCNET_RMD_OWN;
    }

    for (uint32_t i = 0; i < PCNET_TX_DESC_COUNT; i++) {
        uint32_t buffer_address;
        if (!pcnet_dma_address(tx_buffers[i], PCNET_BUFFER_SIZE,
                               &buffer_address)) {
            dbg_print("[PCNET] TX buffer exceeds 32-bit DMA address range\n");
            return false;
        }
        tx_ring[i].base = buffer_address;
        tx_ring[i].length = 0;
        tx_ring[i].status = 0;
        tx_ring[i].misc = 0;
        tx_ring[i].reserved = 0;
    }
    pcnet_barrier();

    init_block.mode = 0x0000;
    init_block.tlen_rlen = (uint16_t)((PCNET_TX_LOG2 << 12) |
                                      (PCNET_RX_LOG2 << 4));
    for (int i = 0; i < 6; i++)
        init_block.phys_addr[i] = mac_address[i];
    init_block.reserved = 0;
    init_block.filter[0] = 0;
    init_block.filter[1] = 0;
    init_block.rx_ring = rx_ring_address;
    init_block.tx_ring = tx_ring_address;
    pcnet_barrier();

    return true;
}

int pcnet_init(uint16_t base) {
    uint32_t init_block_address;

    io_base = base;
    initialized = false;
    rx_index = 0;
    tx_index = 0;

    dbg_print("[PCNET] Init IO=0x");
    dbg_print_hex(io_base);
    dbg_print("\n");

    pcnet_reset();
    if (!pcnet_read_mac()) {
        dbg_print("[PCNET] Invalid MAC address\n");
        io_base = 0;
        return -1;
    }
    pcnet_log_mac();

    /*
     * A 32-bit write to RDP switches the controller from 16-bit register
     * access (DWIO=0) to 32-bit register access (DWIO=1). RAP is CSR0 after
     * reset, so the value written here is harmless.
     */
    outl(io_base + PCNET_IO_RDP, 0);

    /* BCR20 writes are only accepted while the controller is stopped. */
    pcnet_write_csr(PCNET_CSR0, PCNET_CSR0_STOP);
    for (int i = 0; i < 1000 && !(pcnet_read_csr(PCNET_CSR0) & PCNET_CSR0_STOP); i++)
        io_wait();
    if (!(pcnet_read_csr(PCNET_CSR0) & PCNET_CSR0_STOP)) {
        dbg_print("[PCNET] Controller did not enter STOP state\n");
        io_base = 0;
        return -1;
    }

    /* SWSTYLE=2 selects the 32-bit structures and sets SSIZE32. */
    pcnet_write_bcr(PCNET_BCR_SWS, 2);
    uint16_t software_style = pcnet_read_bcr(PCNET_BCR_SWS);
    dbg_print("[PCNET] BCR20=0x");
    dbg_print_hex(software_style);
    dbg_print("\n");
    if (!(software_style & 0x0100)) {
        dbg_print("[PCNET] 32-bit software style was not enabled\n");
        io_base = 0;
        return -1;
    }

    if (!pcnet_setup_rings()) {
        io_base = 0;
        return -1;
    }

    if (!pcnet_dma_address(&init_block, sizeof(init_block),
                           &init_block_address)) {
        dbg_print("[PCNET] Init block exceeds 32-bit DMA address range\n");
        io_base = 0;
        return -1;
    }

    pcnet_write_csr(PCNET_CSR1, (uint16_t)(init_block_address & 0xFFFF));
    pcnet_write_csr(PCNET_CSR2, (uint16_t)(init_block_address >> 16));
    pcnet_write_csr(PCNET_CSR3, 0x0000);
    pcnet_write_csr(PCNET_CSR4, 0x0915); /* automatic transmit padding */

    dbg_print("[PCNET] Init block=0x");
    dbg_print_hex(init_block_address);
    dbg_print(" RX=0x");
    dbg_print_hex(init_block.rx_ring);
    dbg_print(" TX=0x");
    dbg_print_hex(init_block.tx_ring);
    dbg_print("\n");

    pcnet_write_csr(PCNET_CSR0, PCNET_CSR0_INIT);
    bool init_done = false;
    for (int i = 0; i < 100000; i++) {
        if (pcnet_read_csr(PCNET_CSR0) & PCNET_CSR0_IDON) {
            init_done = true;
            break;
        }
        io_wait();
    }
    if (!init_done) {
        dbg_print("[PCNET] Initialization timed out\n");
        io_base = 0;
        return -1;
    }

    pcnet_write_csr(PCNET_CSR0, PCNET_CSR0_STRT);
    bool started = false;
    for (int i = 0; i < 10000; i++) {
        uint16_t csr0 = pcnet_read_csr(PCNET_CSR0);
        if ((csr0 & (PCNET_CSR0_RXON | PCNET_CSR0_TXON)) ==
            (PCNET_CSR0_RXON | PCNET_CSR0_TXON)) {
            started = true;
            break;
        }
        io_wait();
    }
    if (!started) {
        dbg_print("[PCNET] Receiver/transmitter did not start\n");
        io_base = 0;
        return -1;
    }

    dbg_print("[PCNET] CSR0=0x");
    dbg_print_hex(pcnet_read_csr(PCNET_CSR0));
    dbg_print("\n");
    initialized = true;
    return 0;
}

void pcnet_get_mac(uint8_t *mac) {
    if (!mac)
        return;
    for (int i = 0; i < 6; i++)
        mac[i] = mac_address[i];
}

int pcnet_send(const uint8_t *data, uint32_t len) {
    if (!initialized || !data || len == 0 || len > PCNET_BUFFER_SIZE)
        return -1;

    uint32_t index = tx_index;
    if (pcnet_tx_status(index) & PCNET_TMD_OWN) {
        for (int i = 0; i < 100000; i++) {
            if (!(pcnet_tx_status(index) & PCNET_TMD_OWN))
                break;
            io_wait();
        }
        if (pcnet_tx_status(index) & PCNET_TMD_OWN) {
            dbg_print("[PCNET] TX ring full\n");
            return -1;
        }
    }

    uint32_t buffer_address;
    if (!pcnet_dma_address(tx_buffers[index], len, &buffer_address)) {
        dbg_print("[PCNET] TX buffer exceeds 32-bit DMA address range\n");
        return -1;
    }

    for (uint32_t i = 0; i < len; i++)
        tx_buffers[index][i] = data[i];

    tx_ring[index].base = buffer_address;
    tx_ring[index].length = (uint16_t)(-len);
    tx_ring[index].misc = 0;
    tx_ring[index].reserved = 0;
    pcnet_barrier();
    tx_ring[index].status = PCNET_TMD_OWN | PCNET_TMD_STP | PCNET_TMD_ENP;

    tx_index = (index + 1) % PCNET_TX_DESC_COUNT;

    /* Trigger an immediate transmit poll instead of waiting for the timer. */
    pcnet_write_csr(PCNET_CSR0, PCNET_CSR0_TDMD);
    return 0;
}

int pcnet_recv(uint8_t *buf, uint32_t max) {
    if (!initialized || !buf || max < 14)
        return -1;

    uint32_t index = rx_index;
    uint16_t status = pcnet_rx_status(index);
    if (status & PCNET_RMD_OWN)
        return 0;

    uint32_t message_length = rx_ring[index].msg_length & 0x0FFF;
    bool valid = !(status & PCNET_RMD_ERR) &&
                 (status & PCNET_RMD_STP) &&
                 (status & PCNET_RMD_ENP) &&
                 message_length > 4;
    uint32_t frame_length = valid ? message_length - 4 : 0;

    if (frame_length < 14 || frame_length > PCNET_BUFFER_SIZE) {
        if (valid)
            dbg_print("[PCNET] Invalid RX frame length\n");
        valid = false;
    }

    if (valid) {
        if (frame_length > max)
            frame_length = max;
        for (uint32_t i = 0; i < frame_length; i++)
            buf[i] = rx_buffers[index][i];
    }

    rx_index = (index + 1) % PCNET_RX_DESC_COUNT;
    pcnet_barrier();
    rx_ring[index].status = PCNET_RMD_OWN;
    return valid ? (int)frame_length : 0;
}

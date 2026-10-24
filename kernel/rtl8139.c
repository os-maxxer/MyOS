#include <solis/rtl8139.h>
#include <solis/pci.h>
#include <solis/ports.h>
#include <solis/console.h>
#include <solis/dbg.h>

static uint16_t io = 0;
static uint8_t rx_ring[RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t tx_bufs[4][TX_BUF_SIZE] __attribute__((aligned(4)));
static int tx_cur = 0;
static int rx_offset = 0;

#define RTL_REG_MAC  0x00
#define RTL_REG_RBSTART 0x30
#define RTL_REG_CR  0x37
#define RTL_REG_CAPR 0x38
#define RTL_REG_RCR 0x44
#define RTL_REG_TSD 0x10
#define RTL_REG_TSAD 0x20
#define RTL_REG_IMR 0x3C
#define RTL_REG_ISR 0x3E

#define CR_RX_ENABLE 0x08
#define CR_TX_ENABLE 0x04
#define CR_RESET     0x10
#define RCR_AB       0x0F
#define RCR_WRAP     (1 << 7)
#define RCR_64K      (3 << 13)

int rtl8139_init(uint16_t io_base) {
    io = io_base;

    pci_write_config(0, pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID),
                     0, 0x04, 0x05);

    dbg_print("[RTL8139] Init IO=0x");
    dbg_print_hex(io);
    dbg_print("\n");

    outb(io + RTL_REG_CR, CR_RESET);
    int reset_retries = 0;
    while ((inb(io + RTL_REG_CR) & CR_RESET) && reset_retries < 1000) {
        io_wait();
        reset_retries++;
    }
    dbg_print("[RTL8139] Reset done\n");

    uint32_t rbaddr = (uint32_t)(uintptr_t)rx_ring;
    outl(io + RTL_REG_RBSTART, rbaddr);
    dbg_print("[RTL8139] RBSTART=0x");
    dbg_print_hex(rbaddr);
    dbg_print("\n");

    outb(io + RTL_REG_IMR, 0x00);
    outl(io + RTL_REG_RCR, RCR_AB | RCR_WRAP | RCR_64K);
    outb(io + RTL_REG_CR, CR_RX_ENABLE | CR_TX_ENABLE);

    uint8_t mac[6];
    rtl8139_get_mac(mac);
    dbg_print("[RTL8139] MAC=");
    for (int i = 0; i < 6; i++) {
        dbg_print_hex(mac[i]);
        if (i < 5) dbg_print(":");
    }
    dbg_print("\n");

    dbg_print("[RTL8139] CR=");
    dbg_print_hex(inb(io + RTL_REG_CR));
    dbg_print("\n");

	dbg_print("[RTL8139] RX ring first 64 bytes:");
	for (int i = 0; i < 64; i++) {
		if ((i & 0xF) == 0) dbg_print("\n  ");
		dbg_print_hex(rx_ring[i]);
		dbg_print(" ");
	}
	dbg_print("\n");

	return 0;
}

void rtl8139_get_mac(uint8_t *mac) {
    for (int i = 0; i < 6; i++)
        mac[i] = inb(io + RTL_REG_MAC + i);
}

void rtl8139_send(const uint8_t *data, uint32_t len) {
    if (len > TX_BUF_SIZE) len = TX_BUF_SIZE;
    int idx = tx_cur % 4;

    for (uint32_t i = 0; i < len; i++)
        tx_bufs[idx][i] = data[i];

    uint32_t addr = (uint32_t)(uintptr_t)&tx_bufs[idx];
    outl(io + RTL_REG_TSAD + idx * 4, addr);
    outl(io + RTL_REG_TSD + idx * 4, len | 0x10000);

    dbg_print("[RTL8139] TX buf=");
    dbg_print_dec(idx);
    dbg_print(" len=");
    dbg_print_dec(len);
    dbg_print(" addr=0x");
    dbg_print_hex(addr);
    dbg_print("\n");

    tx_cur++;
}

int rtl8139_recv(uint8_t *buf, uint32_t max) {
    volatile uint8_t *vrx = rx_ring;
    uint16_t rx_status = *(volatile uint16_t*)(vrx + rx_offset);
    uint16_t rx_size   = *(volatile uint16_t*)(vrx + rx_offset + 2);

    if (!(rx_status & 0x0001)) {
        io_wait();
        return 0;
    }

    uint32_t frame_len = rx_size & 0x3FFF;
    if (rx_offset + frame_len + 4 > RX_BUF_SIZE) {
        dbg_print("[RTL8139] RX wrap: resetting offset\n");
        rx_offset = 0;
        return 0;
    }

    uint32_t pkt_start = rx_offset + 4;
    uint32_t pkt_len = frame_len - 4;
    if (pkt_len > max) pkt_len = max;
    if (pkt_len < 14) {
        dbg_print("[RTL8139] RX too short\n");
        return 0;
    }

    dbg_print("[RTL8139] RX status=0x");
    dbg_print_hex(rx_status);
    dbg_print(" hdr_len=");
    dbg_print_dec(frame_len);
    dbg_print(" pkt_len=");
    dbg_print_dec(pkt_len);
    dbg_print(" off=");
    dbg_print_dec(rx_offset);
    dbg_print("\n");

    for (uint32_t i = 0; i < pkt_len; i++)
        buf[i] = vrx[pkt_start + i];

    *(volatile uint16_t*)(vrx + rx_offset) = 0;
    *(volatile uint16_t*)(vrx + rx_offset + 2) = 0;

    rx_offset = (rx_offset + 4 + frame_len + 3) & ~3;
    if (rx_offset >= RX_BUF_SIZE) rx_offset = 0;

    outw(io + RTL_REG_CAPR, rx_offset - 0x10);
    return (int)pkt_len;
}

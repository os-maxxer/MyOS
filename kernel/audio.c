#include <solis/audio.h>
#include <solis/pci.h>
#include <solis/pmm.h>
#include <solis/ports.h>
#include <solis/vmm.h>

/* A small AC'97 PCM-out driver.  The codec runs at 48 kHz, stereo, signed
 * 16-bit samples.  We give the bus master a descriptor per 4 KiB page, which
 * avoids assuming that separate page allocations are physically adjacent. */
#define AUDIO_PAGES 32
#define AUDIO_RATE 48000
#define AUDIO_SAMPLES_PER_PAGE (PMM_PAGE_SIZE / 2)

struct audio_descriptor {
    uint32_t address;
    uint16_t length;
    uint16_t flags;
} __attribute__((packed));

static uint8_t *audio_pages[AUDIO_PAGES];
static struct audio_descriptor audio_bdl[AUDIO_PAGES] __attribute__((aligned(16)));
static uint16_t audio_nam;
static uint16_t audio_nabm;
static bool audio_ready;

static const int16_t audio_sine[32] = {
       0,   195,   383,   556,   707,   831,   924,   981,
    1000,   981,   924,   831,   707,   556,   383,   195,
       0,  -195,  -383,  -556,  -707,  -831,  -924,  -981,
   -1000,  -981,  -924,  -831,  -707,  -556,  -383,  -195
};

static void audio_delay(void) {
    for (volatile int i = 0; i < 20000; i++) io_wait();
}

static uint32_t audio_physical(const void *address) {
    uint64_t physical = 0;
    if (vmm_translate((uintptr_t)address, &physical))
        return (uint32_t)physical;
    return (uint32_t)(uintptr_t)address;
}

static bool audio_reset_output(void) {
    outb(audio_nabm + 0x1B, 0x02);
    for (int i = 0; i < 100000; i++) {
        if (!(inb(audio_nabm + 0x1B) & 0x02)) return true;
        io_wait();
    }
    return false;
}

void audio_stop(void) {
    if (!audio_ready) return;
    outb(audio_nabm + 0x1B, 0x00);
    (void)audio_reset_output();
}

int audio_available(void) {
    return audio_ready ? 1 : 0;
}

bool audio_init(void) {
    if (audio_ready) return true;

    struct pci_device_info device;
    bool found = pci_find_class(0x04, 0x01, 0x00, &device);
    if (!found) found = pci_find_device(0x8086, 0x2415, &device);
    if (!found) return false;

    uint32_t bar0 = pci_get_bar(device.bus, device.slot, device.function, 0);
    uint32_t bar1 = pci_get_bar(device.bus, device.slot, device.function, 1);
    if (!(bar0 & 1) || !(bar1 & 1)) return false;
    audio_nam = (uint16_t)(bar0 & 0xFFFC);
    audio_nabm = (uint16_t)(bar1 & 0xFFFC);
    if (!audio_nam || !audio_nabm) return false;

    pci_enable_command_bits(&device, PCI_COMMAND_IO_SPACE |
                                       PCI_COMMAND_BUS_MASTER);

    /* Resume the AC link and reset the mixer to its defaults. */
    outl(audio_nabm + 0x2C, 0x00000002);
    audio_delay();
    outw(audio_nam + 0x00, 0x0000);
    audio_delay();

    /* Unmute the master and PCM outputs.  A zero volume field is 0 dB. */
    outw(audio_nam + 0x02, 0x0000);
    outw(audio_nam + 0x18, 0x0000);
    outw(audio_nam + 0x2C, AUDIO_RATE);

    for (int i = 0; i < AUDIO_PAGES; i++) {
        audio_pages[i] = (uint8_t *)pmm_alloc_page();
        if (!audio_pages[i]) {
            for (int j = 0; j < i; j++) pmm_free_page(audio_pages[j]);
            return false;
        }
        for (int j = 0; j < PMM_PAGE_SIZE; j++) audio_pages[i][j] = 0;
        audio_bdl[i].address = audio_physical(audio_pages[i]);
        audio_bdl[i].length = AUDIO_SAMPLES_PER_PAGE;
        audio_bdl[i].flags = 0;
    }

    if (!audio_reset_output()) {
        for (int i = 0; i < AUDIO_PAGES; i++) pmm_free_page(audio_pages[i]);
        return false;
    }
    audio_ready = true;
    return true;
}

int audio_play_tone(uint32_t hz, uint32_t ms) {
    if (!audio_ready) return 0;
    if (hz < 40) hz = 40;
    if (hz > 12000) hz = 12000;
    if (ms < 20) ms = 20;
    if (ms > 650) ms = 650;

    uint32_t frames = (AUDIO_RATE * ms) / 1000;
    uint32_t samples = frames * 2;
    uint32_t pages = (samples + AUDIO_SAMPLES_PER_PAGE - 1) /
                     AUDIO_SAMPLES_PER_PAGE;
    if (pages > AUDIO_PAGES) pages = AUDIO_PAGES;
    if (!pages) pages = 1;

    uint32_t phase = 0;
    uint32_t step = (uint32_t)(((uint64_t)hz * 32u * 65536u) / AUDIO_RATE);
    uint32_t written = 0;
    for (uint32_t page = 0; page < pages; page++) {
        uint32_t count = samples - written;
        if (count > AUDIO_SAMPLES_PER_PAGE) count = AUDIO_SAMPLES_PER_PAGE;
        if (page + 1 == pages) count = samples - written;
        uint8_t *data = audio_pages[page];
        for (uint32_t i = 0; i < count; i++) {
            int16_t sample = (int16_t)((audio_sine[(phase >> 16) & 31] * 9) / 10);
            data[i * 2] = (uint8_t)((uint16_t)sample & 0xFF);
            data[i * 2 + 1] = (uint8_t)((uint16_t)sample >> 8);
            phase += step;
        }
        audio_bdl[page].address = audio_physical(data);
        audio_bdl[page].length = (uint16_t)count;
        audio_bdl[page].flags = (page + 1 == pages) ? 0x4000 : 0;
        written += count;
        if (written >= samples) break;
    }

    audio_stop();
    outl(audio_nabm + 0x10, audio_physical(audio_bdl));
    outb(audio_nabm + 0x15, (uint8_t)(pages - 1));
    outb(audio_nabm + 0x1B, 0x01);
    return 1;
}

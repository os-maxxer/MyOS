#include <solis/acpi.h>
#include <solis/dbg.h>
#include <solis/console.h>

#define ACPI_SEARCH_START 0x000E0000u
#define ACPI_SEARCH_END   0x00100000u
#define ACPI_IDENTITY_MAP_LIMIT (512ULL * 1024 * 1024 * 1024)
#define ACPI_MAX_RSDP_LENGTH 4096
#define ACPI_RSDP_SIG     "RSD PTR "
#define ACPI_XSDT_SIG     "XSDT"
#define ACPI_RSDT_SIG     "RSDT"
#define ACPI_FACP_SIG     "FACP"
#define ACPI_DSDT_SIG     "DSDT"

static bool acpi_available = false;
static uint64_t rsdt_address = 0;
static uint64_t xsdt_address = 0;
static uint64_t dsdt_address = 0;

static int acpi_memcmp(const void *a, const void *b, uint32_t count);
static uint8_t acpi_checksum(const void *ptr, uint32_t length);

static bool acpi_address_is_mapped(uint64_t address, uint64_t size) {
    return address != 0 && address < ACPI_IDENTITY_MAP_LIMIT &&
           size <= ACPI_IDENTITY_MAP_LIMIT - address;
}

static const struct acpi_sdt_header *acpi_get_valid_sdt(uint64_t address,
                                                         const char *signature) {
    if (!acpi_address_is_mapped(address, sizeof(struct acpi_sdt_header))) return 0;

    const struct acpi_sdt_header *header =
        (const struct acpi_sdt_header *)(uintptr_t)address;
    if (header->length < sizeof(*header) ||
        !acpi_address_is_mapped(address, header->length) ||
        acpi_checksum(header, header->length) != 0 ||
        (signature && acpi_memcmp(header->signature, signature, 4) != 0))
        return 0;

    return header;
}

static int acpi_memcmp(const void *a, const void *b, uint32_t count) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (uint32_t i = 0; i < count; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static uint8_t acpi_checksum(const void *ptr, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += bytes[i];
    return sum;
}

static void *acpi_scan_rsdp(void) {
    for (uint32_t addr = ACPI_SEARCH_START; addr < ACPI_SEARCH_END; addr += 16) {
        uint8_t *ptr = (uint8_t *)(uintptr_t)addr;
        if (acpi_memcmp(ptr, ACPI_RSDP_SIG, 8) == 0) {
            struct acpi_rsdp_v1 *rsdp = (struct acpi_rsdp_v1 *)ptr;
            if (acpi_checksum(rsdp, 20) == 0) {
                return ptr;
            }
        }
    }
    return 0;
}

static bool acpi_match_token(const uint8_t *buf, uint32_t len, const char *token, uint32_t *out_pos) {
    uint32_t token_len = 0;
    while (token[token_len] != '\0') token_len++;
    for (uint32_t i = 0; i + token_len <= len; i++) {
        if (acpi_memcmp(buf + i, token, token_len) == 0) {
            if (out_pos) *out_pos = i;
            return true;
        }
    }
    return false;
}

static bool acpi_scan_for_hid(const uint8_t *buf, uint32_t len, struct acpi_touchpad_hints *out) {
    static const char *candidates[] = {"PNP0C50", "ELAN", "SYNA", "INT33C3", "PNP0F13"};
    for (int i = 0; i < 5; i++) {
        uint32_t pos = 0;
        if (acpi_match_token(buf, len, candidates[i], &pos)) {
            int copy_len = 15;
            if (copy_len > (int)len - (int)pos) copy_len = (int)len - (int)pos;
            int n = 0;
            while (n < copy_len && buf[pos + n] != 0 && buf[pos + n] != '\n' && buf[pos + n] != '\r') {
                out->hid[n] = (char)buf[pos + n];
                n++;
            }
            out->hid[n] = '\0';
            out->has_hid = true;
            out->found = true;
            return true;
        }
    }
    return false;
}

static void acpi_scan_for_methods(const uint8_t *buf, uint32_t len, struct acpi_touchpad_hints *out) {
    if (acpi_match_token(buf, len, "_CRS", 0)) out->has_crs = true;
    if (acpi_match_token(buf, len, "_PS0", 0)) out->has_ps0 = true;

    for (uint32_t i = 0; i + 4 < len; i++) {
        if ((buf[i] == 0x15 || buf[i] == 0x2C || buf[i] == 0x1C || buf[i] == 0x5C || buf[i] == 0x6C) &&
            (buf[i + 1] == 0x00 || buf[i + 1] == 0x01 || buf[i + 1] == 0x02 || buf[i + 1] == 0x04)) {
            out->i2c_address = buf[i];
        }
        if (buf[i] == 0x00 && i + 2 < len && buf[i + 1] == 0x00 && buf[i + 2] <= 0x1F) {
            out->gpio_pin = buf[i + 2];
        }
        if (buf[i] == 0x00 && i + 3 < len && buf[i + 1] == 0x00 && buf[i + 2] == 0x00 && buf[i + 3] <= 0x20) {
            out->irq = buf[i + 3];
        }
    }
}

bool acpi_scan_touchpad_hints(struct acpi_touchpad_hints *out) {
    if (!out) return false;
    out->found = false;
    out->has_hid = false;
    out->has_crs = false;
    out->has_ps0 = false;
    out->hid[0] = '\0';
    out->i2c_address = 0;
    out->irq = 0;
    out->gpio_pin = 0;
    out->dsdt_offset = 0;

    if (!acpi_is_available() || dsdt_address == 0) return false;

    const uint8_t *dsdt = (const uint8_t *)(uintptr_t)dsdt_address;
    const struct acpi_sdt_header *hdr = (const struct acpi_sdt_header *)dsdt;
    uint32_t len = hdr->length;
    if (len == 0) return false;

    uint32_t token_pos = 0;
    if (acpi_match_token(dsdt, len, "_HID", &token_pos) || acpi_match_token(dsdt, len, "_CID", &token_pos)) {
        out->dsdt_offset = token_pos;
    }

    acpi_scan_for_hid(dsdt, len, out);
    acpi_scan_for_methods(dsdt, len, out);

    if (out->has_hid || out->has_crs || out->has_ps0) {
        out->found = true;
        return true;
    }

    return false;
}

bool acpi_init(void) {
    acpi_available = false;
    rsdt_address = 0;
    xsdt_address = 0;
    dsdt_address = 0;

    void *rsdp_ptr = acpi_scan_rsdp();
    if (!rsdp_ptr) {
        dbg_set_driver_state("acpi", DBG_DRIVER_FAILED);
        return false;
    }

    struct acpi_rsdp_v1 *rsdp = (struct acpi_rsdp_v1 *)rsdp_ptr;
    rsdt_address = rsdp->rsdt_address;
    if (rsdp->revision >= 2) {
        struct acpi_rsdp_v2 *rsdp2 = (struct acpi_rsdp_v2 *)rsdp_ptr;
        if (rsdp2->length >= sizeof(*rsdp2) &&
            rsdp2->length <= ACPI_MAX_RSDP_LENGTH &&
            acpi_checksum(rsdp2, rsdp2->length) == 0)
            xsdt_address = rsdp2->xsdt_address;
    }

    const struct acpi_sdt_header *root = 0;
    bool root_is_xsdt = false;
    if (xsdt_address != 0) {
        root = acpi_get_valid_sdt(xsdt_address, ACPI_XSDT_SIG);
        root_is_xsdt = root != 0;
    }
    if (!root && rsdt_address != 0)
        root = acpi_get_valid_sdt(rsdt_address, ACPI_RSDT_SIG);
    if (!root) {
        dbg_set_driver_state("acpi", DBG_DRIVER_FAILED);
        return false;
    }
    dbg_print(root_is_xsdt ? "[ACPI] XSDT active\n" : "[ACPI] RSDT active\n");

    uint32_t entry_size = root_is_xsdt ? sizeof(uint64_t) : sizeof(uint32_t);
    uint32_t entries_size = root->length - sizeof(*root);
    if (entries_size % entry_size != 0) {
        dbg_set_driver_state("acpi", DBG_DRIVER_FAILED);
        return false;
    }
    uint32_t entry_count = entries_size / entry_size;
    const uint8_t *entries = (const uint8_t *)root + sizeof(*root);
    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t entry = 0;
        if (root_is_xsdt) {
            for (uint32_t byte = 0; byte < sizeof(entry); byte++)
                entry |= (uint64_t)entries[i * entry_size + byte] << (byte * 8);
        } else {
            for (uint32_t byte = 0; byte < sizeof(uint32_t); byte++)
                entry |= (uint64_t)entries[i * entry_size + byte] << (byte * 8);
        }
        if (!entry) continue;

        const struct acpi_sdt_header *table = acpi_get_valid_sdt(entry, 0);
        if (!table || acpi_memcmp(table->signature, ACPI_FACP_SIG, 4) != 0 ||
            table->length < offsetof(struct acpi_facp, dsdt) + sizeof(uint32_t))
            continue;

        const struct acpi_facp *facp = (const struct acpi_facp *)table;
        dsdt_address = facp->dsdt;
        if (table->length >= offsetof(struct acpi_facp, x_dsdt) + sizeof(uint64_t) &&
            facp->x_dsdt != 0 &&
            acpi_get_valid_sdt(facp->x_dsdt, ACPI_DSDT_SIG))
            dsdt_address = facp->x_dsdt;

        if (acpi_get_valid_sdt(dsdt_address, ACPI_DSDT_SIG)) {
            acpi_available = true;
            dbg_set_driver_state("acpi", DBG_DRIVER_ACTIVE);
            dbg_set_pipeline_stage("acpi");
            struct acpi_touchpad_hints hints;
            if (acpi_scan_touchpad_hints(&hints)) {
                dbg_print("[ACPI] touchpad hints found\n");
                dbg_print("[ACPI] HID: "); dbg_print(hints.hid[0] ? hints.hid : "unknown"); dbg_print("\n");
                dbg_print("[ACPI] _CRS: "); dbg_print(hints.has_crs ? "yes" : "no"); dbg_print("\n");
                dbg_print("[ACPI] _PS0: "); dbg_print(hints.has_ps0 ? "yes" : "no"); dbg_print("\n");
            }
            return true;
        }
    }

    dbg_set_driver_state("acpi", DBG_DRIVER_FAILED);
    return false;
}

uint64_t acpi_get_rsdt_address(void) {
    return rsdt_address;
}

uint64_t acpi_get_xsdt_address(void) {
    return xsdt_address;
}

uint64_t acpi_get_dsdt_address(void) {
    return dsdt_address;
}

bool acpi_is_available(void) {
    return acpi_available;
}

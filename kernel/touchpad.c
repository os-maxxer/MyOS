#include <solis/touchpad.h>
#include <solis/i2c.h>
#include <solis/console.h>
#include <solis/timer.h>
#include <solis/dbg.h>
#include <solis/acpi.h>

#define HID_I2C_ADDR_1      0x15
#define HID_I2C_ADDR_2      0x2C
#define HID_I2C_ADDR_3      0x1C
#define HID_I2C_ADDR_4      0x5C
#define HID_I2C_ADDR_5      0x6C

#define HID_REG_DESC        0x01
#define HID_REG_REPORT      0x02
#define HID_REG_COMMAND     0x04
#define HID_REG_RESET       0x04

#define HID_CMD_RESET       0x01
#define HID_CMD_GET_REPORT  0x02
#define HID_CMD_SET_POWER   0x03
#define HID_POWER_ON        0x00
#define HID_POWER_SLEEP     0x01

/* Command register writes pack the command in the high nibble and the
 * parameter in the low nibble, e.g. SET_POWER + FULL ON == 0x30. */
#define HID_CMD_WRITE(cmd, param) (((cmd) << 4) | (param))

#define MAX_TOUCHPAD_ATTEMPT 3
#define HID_DESC_SIZE 256
#define REPORT_BUF_SIZE 32

static uint16_t touchpad_address = 0;
static bool touchpad_found = false;
static bool touchpad_initialized = false;

static int touchpad_reset(void) {
    /* Reset is a command-register write of 0x01; the device clears the
     * register back to zero once the reset has completed. */
    if (i2c_smbus_write_byte(touchpad_address, HID_REG_RESET, HID_CMD_RESET) != 0)
        return -1;
    uint32_t timeout = 50000;
    while (timeout--) {
        uint8_t sts = 0;
        if (i2c_smbus_read_byte(touchpad_address, HID_REG_COMMAND, &sts) == 0) {
            if (sts == 0) return 0;
        }
    }
    return -1;
}

static bool detect_touchpad_at(uint16_t addr) {
    if (!i2c_detect_device(addr)) return false;

    uint8_t desc[8] = {0};
    if (i2c_smbus_read_block(addr, HID_REG_DESC, desc, 8) != 0)
        return false;

    uint16_t desc_len = desc[0] | (desc[1] << 8);
    if (desc_len < 4 || desc_len > HID_DESC_SIZE)
        return false;

    /* Many modern HID-over-I2C touchpads report descriptor length or version
     * data in a slightly different layout than the old fixed assumption. For
     * compatibility, accept the device once it responds on a known HID-I2C
     * address and is able to reset successfully. */
    touchpad_address = addr;
    return true;
}

void touchpad_init(void) {
    touchpad_found = false;
    touchpad_initialized = false;
    dbg_set_driver_state("touchpad", DBG_DRIVER_UNINITIALIZED);

    struct acpi_touchpad_hints acpi_hints;
    if (acpi_scan_touchpad_hints(&acpi_hints)) {
        dbg_print("[ACPI] touchpad HID hints: ");
        dbg_print(acpi_hints.hid[0] ? acpi_hints.hid : "unknown");
        dbg_print("\n");
    }

    uint16_t addrs[] = {HID_I2C_ADDR_1, HID_I2C_ADDR_2, HID_I2C_ADDR_3, HID_I2C_ADDR_4,
                        HID_I2C_ADDR_5, 0x19, 0x2D, 0x25, 0x1D, 0x38, 0x36, 0};

    for (int i = 0; addrs[i]; i++) {
        if (detect_touchpad_at(addrs[i])) {
            /* Per the HID-over-I2C spec, bring the device to FULL ON power
             * before issuing the reset. */
            if (i2c_smbus_write_byte(touchpad_address, HID_REG_COMMAND,
                                     HID_CMD_WRITE(HID_CMD_SET_POWER, HID_POWER_ON)) == 0 &&
                touchpad_reset() == 0) {
                touchpad_found = true;
                touchpad_initialized = true;
                dbg_set_driver_state("touchpad", DBG_DRIVER_ACTIVE);
                return;
            }
        }
    }

    dbg_set_driver_state("touchpad", DBG_DRIVER_FAILED);
}

static unsigned int report_buf[REPORT_BUF_SIZE];

static int read_input_report(void) {
    uint8_t buf[REPORT_BUF_SIZE];
    if (i2c_smbus_read_block(touchpad_address, HID_REG_REPORT, buf, REPORT_BUF_SIZE) != 0)
        return -1;

    /* HID-over-I2C reports are not always length-prefixed the same way between
     * vendors. The common pattern is either: [report_id][payload...] or
     * [payload_len][payload...]. We keep the payload and let the parser be
     * forgiving below. */
    int len = 0;
    if (buf[0] >= 1 && buf[0] <= 32) {
        len = buf[0];
        if (len > REPORT_BUF_SIZE) len = REPORT_BUF_SIZE;
        for (int i = 0; i < len; i++) report_buf[i] = buf[i];
        return len;
    }

    for (int i = 0; i < REPORT_BUF_SIZE; i++) {
        if (buf[i] == 0 && i > 0) break;
        report_buf[i] = buf[i];
        len++;
    }
    return len;
}

bool touchpad_poll(struct touchpad_state *state) {
    if (!touchpad_initialized || !state) return false;

    state->dx = 0;
    state->dy = 0;
    state->buttons = 0;
    state->present = touchpad_found;

    int rlen = read_input_report();
    if (rlen <= 0) return false;

    /* Small, tolerant parser for HID touch reports. We accept either report-id
     * prefixed packets or raw payloads and look for a likely x/y coordinate pair
     * near the front of the packet. */
    static int prev_x = 0;
    static int prev_y = 0;
    static bool has_prev = false;

    int start = 0;
    if (rlen > 1 && report_buf[0] <= 4) start = 1;

    for (int i = start; i + 5 < rlen; i++) {
        uint16_t x = (uint16_t)report_buf[i + 1] | ((uint16_t)report_buf[i + 2] << 8);
        uint16_t y = (uint16_t)report_buf[i + 3] | ((uint16_t)report_buf[i + 4] << 8);

        if (x > 8192 || y > 8192) continue;

        if (has_prev) {
            int dx = (int)x - prev_x;
            int dy = (int)y - prev_y;
            if (dx > 127) dx = 127;
            if (dx < -127) dx = -127;
            if (dy > 127) dy = 127;
            if (dy < -127) dy = -127;
            state->dx = dx;
            state->dy = -dy;
        }

        prev_x = x;
        prev_y = y;
        has_prev = true;

        if (rlen > i + 5 && report_buf[i] & 0x01) {
            state->buttons |= 0x01;
        }
        return true;
    }

    return false;
}

bool touchpad_present(void) {
    return touchpad_found;
}

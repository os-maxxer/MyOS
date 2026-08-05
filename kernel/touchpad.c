#include <solis/touchpad.h>
#include <solis/i2c.h>
#include <solis/console.h>
#include <solis/timer.h>

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

    uint8_t desc[4] = {0};
    if (i2c_smbus_read_block(addr, HID_REG_DESC, desc, 4) != 0)
        return false;

    uint16_t desc_len = desc[0] | (desc[1] << 8);
    uint16_t version = desc[2] | (desc[3] << 8);

    if (desc_len < 4 || desc_len > HID_DESC_SIZE)
        return false;

    // Check for valid HID version
    if (version == 0x0100 || version == 0x0101 || version == 0x0110 || version == 0x0111) {
        touchpad_address = addr;
        return true;
    }

    return false;
}

void touchpad_init(void) {
    uint16_t addrs[] = {HID_I2C_ADDR_1, HID_I2C_ADDR_2, HID_I2C_ADDR_3, HID_I2C_ADDR_4, HID_I2C_ADDR_5, 0};

    for (int i = 0; addrs[i]; i++) {
        if (detect_touchpad_at(addrs[i])) {
            /* Per the HID-over-I2C spec, bring the device to FULL ON power
             * before issuing the reset. */
            if (i2c_smbus_write_byte(touchpad_address, HID_REG_COMMAND,
                                     HID_CMD_WRITE(HID_CMD_SET_POWER, HID_POWER_ON)) == 0 &&
                touchpad_reset() == 0) {
                touchpad_found = true;
                touchpad_initialized = true;
                return;
            }
        }
    }
}

static unsigned int report_buf[REPORT_BUF_SIZE];
static int report_len = 0;

static int read_input_report(void) {
    uint8_t buf[REPORT_BUF_SIZE];
    if (i2c_smbus_read_block(touchpad_address, HID_REG_REPORT, buf, REPORT_BUF_SIZE) != 0)
        return -1;

    report_len = buf[0];
    if (report_len > REPORT_BUF_SIZE) report_len = REPORT_BUF_SIZE;
    for (int i = 0; i < report_len; i++)
        report_buf[i] = buf[i];
    return report_len;
}

bool touchpad_poll(struct touchpad_state *state) {
    if (!touchpad_initialized || !state) return false;

    state->dx = 0;
    state->dy = 0;
    state->buttons = 0;
    state->present = touchpad_found;

    int rlen = read_input_report();
    if (rlen < 0) return false;

    // Parse HID touch report (simplified - most touchpads use a standard report format)
    // The report typically has: report_id(1), buttons(1/2), finger_count(1), touch_data per finger
    // Each touch: status(1), x(2), y(2), pressure(1), contact_width(1)

    if (rlen < 3) return false;

    int offset = 1;
    uint8_t report_id = report_buf[0];
    (void)report_id;

    // Check for touch report (usually report_id 1 = touch data)
    if (rlen < 7) return false;

    // Parse at least one finger
    uint8_t status = report_buf[offset + 1];
    if (status & 0x80) {
        // Tip switch / contact valid
        uint8_t contact_count = report_buf[2] & 0x0F;

        if (contact_count == 0) {
            // No contacts - no movement
            return true;
        }

        // Track position for delta calculation
        static int prev_x = 0;
        static int prev_y = 0;
        static bool has_prev = false;

        int x, y;
        if (rlen >= offset + 6) {
            x = report_buf[offset + 3] | (report_buf[offset + 4] << 8);
            y = report_buf[offset + 5] | (report_buf[offset + 6] << 8);
        } else if (rlen >= offset + 4) {
            x = report_buf[offset + 2] | (report_buf[offset + 3] << 8);
            y = report_buf[offset + 4];
        } else {
            return false;
        }

        // Button states
        if (rlen > 1) {
            if (report_buf[1] & 0x01) state->buttons |= 0x01;
            if (report_buf[1] & 0x02) state->buttons |= 0x02;
        }

        if (has_prev) {
            state->dx = x - prev_x;
            state->dy = y - prev_y;
            /* Clamp so a bogus report can't fling the pointer across the
             * screen; also flips Y so up is up. */
            if (state->dx > 127) state->dx = 127;
            if (state->dx < -127) state->dx = -127;
            if (state->dy > 127) state->dy = 127;
            if (state->dy < -127) state->dy = -127;
            state->dy = -state->dy;
        }

        prev_x = x;
        prev_y = y;
        has_prev = true;
    }

    return true;
}

bool touchpad_present(void) {
    return touchpad_found;
}

#include <solis/keyboard.h>
#include <solis/ports.h>
#include <solis/pic.h>
#include <solis/idt.h>
#include <solis/console.h>
#include <solis/dbg.h>

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEY_BUFFER_SIZE 256

static volatile uint8_t key_buffer[KEY_BUFFER_SIZE];
static volatile uint8_t key_head = 0;
static volatile uint8_t key_tail = 0;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;
static bool extended = false;
static volatile bool start_menu_pressed = false;
static volatile int func_key_pressed = 0;
static uint8_t hid_previous_keys[6];
static uint8_t hid_previous_modifiers;
static bool hid_caps_lock = false;

static uint8_t keyboard_hid_scancode(uint8_t usage) {
    static const uint8_t letter_scancodes[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
        0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
        0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
    };
    static const uint8_t digit_scancodes[10] = {
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B
    };
    if (usage >= 0x04 && usage <= 0x1D)
        return letter_scancodes[usage - 0x04];
    if (usage >= 0x1E && usage <= 0x27)
        return digit_scancodes[usage - 0x1E];
    if (usage >= 0x3A && usage <= 0x45) {
        uint8_t index = usage - 0x3A;
        return index < 10 ? (uint8_t)(0x3B + index)
                          : (uint8_t)(0x57 + index - 10);
    }

    switch (usage) {
        case 0x28: return 0x1C;
        case 0x29: return 0x01;
        case 0x2A: return 0x0E;
        case 0x2B: return 0x0F;
        case 0x2C: return 0x39;
        case 0x2D: return 0x0C;
        case 0x2E: return 0x0D;
        case 0x2F: return 0x1A;
        case 0x30: return 0x1B;
        case 0x31: return 0x2B;
        case 0x33: return 0x27;
        case 0x34: return 0x28;
        case 0x35: return 0x29;
        case 0x36: return 0x33;
        case 0x37: return 0x34;
        case 0x38: return 0x35;
        case 0x39: return 0x3A;
        default: return 0;
    }
}

static const char scancode_ascii_normal[128] = {
    0,   0x1b,'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b','\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_ascii_shift[128] = {
    0,   0x1b,'!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b','\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void keyboard_enqueue(char c) {
    uint8_t next = (key_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_tail) {
        key_buffer[key_head] = c;
        key_head = next;
    }
}

/* Decode one keyboard scancode. Shared by the IRQ path and the polling
 * drain so both behave identically. */
static void keyboard_process(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended = true;
        return;
    }

    if (extended) {
        extended = false;
        if (scancode & 0x80) {
            if (scancode == 0xC8) keyboard_enqueue(0x8A); /* up release */
            else if (scancode == 0xD0) keyboard_enqueue(0x8B); /* down release */
            else if (scancode == 0xCB) keyboard_enqueue(0x8C); /* left release */
            else if (scancode == 0xCD) keyboard_enqueue(0x8D); /* right release */
            return;
        }
        if (scancode == 0x5B)
            start_menu_pressed = true;
        else if (scancode == 0x53)
            keyboard_enqueue(0x7F); /* Delete */
        else if (scancode == 0x48)
            keyboard_enqueue(0x80); /* Doom / arrow up */
        else if (scancode == 0x50)
            keyboard_enqueue(0x81); /* Doom / arrow down */
        else if (scancode == 0x4B)
            keyboard_enqueue(0x82); /* Doom / arrow left */
        else if (scancode == 0x4D)
            keyboard_enqueue(0x83); /* Doom / arrow right */
        return;
    }

    bool released = scancode & 0x80;
    scancode &= 0x7F;

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = !released;
        keyboard_enqueue(released ? 0x87 : 0x86);
        return;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = !released;
        keyboard_enqueue(released ? 0x85 : 0x84);
        return;
    }
    if (scancode == 0x38) {
        alt_pressed = !released;
        keyboard_enqueue(released ? 0x89 : 0x88);
        return;
    }
    if (scancode == 0x3A && !released) {
        caps_lock = !caps_lock;
        return;
    }
    if (scancode >= 0x3B && scancode <= 0x44) {
        if (!released) func_key_pressed = scancode - 0x3B + 1;
        return;
    }
    if (scancode == 0x57) {
        if (!released) func_key_pressed = 11;
        return;
    }
    if (scancode == 0x58) {
        if (!released) func_key_pressed = 12;
        return;
    }

    if (released) {
        return;
    }

    char c = 0;
    bool use_shift = shift_pressed != caps_lock;
    if (scancode < 128) {
        c = use_shift ? scancode_ascii_shift[scancode] : scancode_ascii_normal[scancode];
    }

    if (c && ctrl_pressed) {
        c = (c >= 'a' && c <= 'z') ? (c - 'a' + 1) : 0;
    }

    if (c)
        keyboard_enqueue(c);
}

static void keyboard_handler(void) {
    /* The keyboard and the mouse share port 0x60. Only consume bytes that
     * are actually ours: the AUX bit in the status byte marks mouse data,
     * and IRQ12 owns it. Without this check the two drivers steal each
     * other's bytes, so a mouse packet header (0x08) decodes as the scancode
     * for '3' and the mouse state machine is fed scancodes, which
     * permanently desyncs the pointer. */
    for (int guard = 0; guard < 32; guard++) {
        uint8_t status = inb(KEYBOARD_STATUS_PORT);
        if (!(status & 0x01)) break;          /* output buffer empty */
        if (status & 0x20) break;             /* AUX byte: leave it for IRQ12 */
        keyboard_process(inb(KEYBOARD_DATA_PORT));
    }
    pic_send_eoi(1);
}

bool keyboard_was_start_menu_pressed(void) {
    bool ret = start_menu_pressed;
    start_menu_pressed = false;
    return ret;
}

int keyboard_consume_func_key(void) {
    int ret = func_key_pressed;
    func_key_pressed = 0;
    return ret;
}

static bool keyboard_hid_key_was_pressed(uint8_t usage) {
    for (uint32_t i = 0; i < 6; i++)
        if (hid_previous_keys[i] == usage) return true;
    return false;
}

void keyboard_submit_hid_report(uint8_t modifiers, const uint8_t keys[6]) {
    if (!keys) return;

    shift_pressed = (modifiers & 0x22) != 0;
    ctrl_pressed = (modifiers & 0x11) != 0;
    alt_pressed = (modifiers & 0x44) != 0;
    if ((modifiers & 0x88) && !(hid_previous_modifiers & 0x88))
        start_menu_pressed = true;

    bool rollover = false;
    for (uint32_t i = 0; i < 6; i++)
        if (keys[i] == 1) rollover = true;
    if (rollover) return;

    for (uint32_t i = 0; i < 6; i++) {
        uint8_t usage = keys[i];
        if (!usage || keyboard_hid_key_was_pressed(usage)) continue;
        if (usage == 0x39) {
            hid_caps_lock = !hid_caps_lock;
            caps_lock = hid_caps_lock;
            continue;
        }
        uint8_t scancode = keyboard_hid_scancode(usage);
        if (scancode) keyboard_process(scancode);
    }
    for (uint32_t i = 0; i < 6; i++)
        hid_previous_keys[i] = keys[i];
    hid_previous_modifiers = modifiers;
}

void keyboard_init(void) {
    dbg_set_driver_state("keyboard", DBG_DRIVER_ACTIVE);
    irq_register_handler(1, keyboard_handler);
    pic_enable_irq(1);
}

bool keyboard_has_input(void) {
    return key_head != key_tail;
}

bool keyboard_read_char(char *out) {
    if (!keyboard_has_input()) {
        return false;
    }
    *out = (char)key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUFFER_SIZE;
    return true;
}

bool keyboard_is_shift_pressed(void) {
    return shift_pressed;
}

bool keyboard_is_ctrl_pressed(void) {
    return ctrl_pressed;
}

bool keyboard_is_alt_pressed(void) {
    return alt_pressed;
}

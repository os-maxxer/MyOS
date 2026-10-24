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

/* Decode one keyboard scancode. Shared by the IRQ path and the polling
 * drain so both behave identically. */
static void keyboard_process(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended = true;
        return;
    }

    if (extended) {
        extended = false;
        if (scancode == 0x5B && !(scancode & 0x80)) {
            start_menu_pressed = true;
        }
        return;
    }

    bool released = scancode & 0x80;
    scancode &= 0x7F;

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = !released;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = !released;
        return;
    }
    if (scancode == 0x38) {
        alt_pressed = !released;
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

    if (c) {
        uint8_t next = (key_head + 1) % KEY_BUFFER_SIZE;
        if (next != key_tail) {
            key_buffer[key_head] = c;
            key_head = next;
        }
    }
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

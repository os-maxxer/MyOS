/*
 * Doom for Solis.
 *
 * Doom's generic main loop is split into a one-time D_DoomLoop setup and a
 * per-frame doomgeneric_Tick() call, so the desktop keeps running between
 * frames. The first tick performs the deferred initialization: this keeps
 * Standard and Minimal installs from paying for Doom at boot, and keeps a
 * missing or corrupt WAD from taking down the whole system.
 */
#include <solis/doom.h>
#include <solis/spx.h>
#include <solis/syscall.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#define KEY_QUEUE_SIZE 64
#define LOADING_TITLE "DOOM  /  SHAREWARE EPISODE"

struct key_event {
    int pressed;
    unsigned char key;
};

static const struct syscall_table *SYS =
    (const struct syscall_table *)SYSCALL_TABLE_ADDR;

static struct key_event key_queue[KEY_QUEUE_SIZE];
static int key_read;
static int key_write;

static bool doom_started;
static bool doom_ready;
static bool doom_failed;
static bool doom_quit;
static bool frame_ready;

static void *doom_jump[8];
static char doom_error[160];

void DG_Error(const char *message) {
    doom_failed = true;
    if (message) {
        size_t i = 0;
        while (i + 1 < sizeof(doom_error) && message[i]) {
            doom_error[i] = message[i];
            i++;
        }
        doom_error[i] = '\0';
    }
    if (doom_jump[0]) {
        __builtin_longjmp(doom_jump, 1);
    }
    for (;;) __asm__ volatile("cli; hlt");
}

void DG_Quit(void) {
    doom_quit = true;
}

static void push_key(int pressed, unsigned char key) {
    int next = (key_write + 1) % KEY_QUEUE_SIZE;
    if (next == key_read) return;
    key_queue[key_write].pressed = pressed;
    key_queue[key_write].key = key;
    key_write = next;
}

void DG_Init(void) {
}

void DG_DrawFrame(void) {
    frame_ready = true;
}

uint32_t DG_GetTicksMs(void) {
    return SYS->get_ticks() * 10u;
}

void DG_SleepMs(uint32_t ms) {
    /* Doom asks for millisecond naps while it waits for the next tic. The
     * scheduler keeps interrupts enabled, so a short pause is kinder to the
     * desktop than spinning through the fallback path. */
    volatile uint32_t spin = ms ? ms * 12000u : 1000u;
    while (spin--) __asm__ volatile("pause");
}

int DG_GetKey(int *pressed, unsigned char *key) {
    if (key_read == key_write) return 0;
    if (pressed) *pressed = key_queue[key_read].pressed;
    if (key) *key = key_queue[key_read].key;
    key_read = (key_read + 1) % KEY_QUEUE_SIZE;
    return 1;
}

void DG_SetWindowTitle(const char *title) {
    (void)title;
}

static unsigned char map_ascii(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (unsigned char)(ch - 'A' + 'a');
    if (ch == '\b') return KEY_BACKSPACE;
    if (ch == '\n' || ch == '\r') return KEY_ENTER;
    return ch;
}

void doom_app_handle_key(char key) {
    unsigned char ch = (unsigned char)key;
    switch (ch) {
        case 0x80: push_key(1, KEY_UPARROW); break;
        case 0x81: push_key(1, KEY_DOWNARROW); break;
        case 0x82: push_key(1, KEY_LEFTARROW); break;
        case 0x83: push_key(1, KEY_RIGHTARROW); break;
        case 0x84: push_key(1, KEY_FIRE); break;
        case 0x85: push_key(0, KEY_FIRE); break;
        case 0x86: push_key(1, KEY_RSHIFT); break;
        case 0x87: push_key(0, KEY_RSHIFT); break;
        case 0x88: push_key(1, KEY_RALT); break;
        case 0x89: push_key(0, KEY_RALT); break;
        case 0x8A: push_key(0, KEY_UPARROW); break;
        case 0x8B: push_key(0, KEY_DOWNARROW); break;
        case 0x8C: push_key(0, KEY_LEFTARROW); break;
        case 0x8D: push_key(0, KEY_RIGHTARROW); break;
        case 0x7f: push_key(1, KEY_DEL); break;
        default:
            if (ch == ' ') push_key(1, KEY_USE);
            else if (ch >= 32 || ch == '\b' || ch == '\n' || ch == '\r' || ch == '\t')
                push_key(1, map_ascii(ch));
            break;
    }
}

static void draw_loading(int x, int y, int w, int h, const char *message) {
    SYS->fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                   0xFF090B12);
    SYS->draw_string((uint32_t)(x + 24), (uint32_t)(y + 24),
                     LOADING_TITLE, 0xFFE05B5B);
    SYS->draw_string((uint32_t)(x + 24), (uint32_t)(y + 48),
                     message ? message : "Preparing the shareware episode...",
                     0xFFF3F7FF);
    SYS->draw_string((uint32_t)(x + 24), (uint32_t)(y + 76),
                     "Arrows move  /  Ctrl fire  /  Space use  /  Esc menu",
                     0xFF9AA8BD);
}

static void start_doom(void) {
    doom_started = true;
    static char arg0[] = "doom";
    static char arg1[] = "-iwad";
    static char arg2[] = "doom1.wad";
    static char arg3[] = "-mb";
    static char arg4[] = "6";
    static char arg5[] = "-nosound";
    static char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, NULL};

    if (__builtin_setjmp(doom_jump) == 0) {
        doomgeneric_Create(6, argv);
        doom_ready = true;
    } else {
        doom_failed = true;
        doom_ready = false;
    }
    doom_jump[0] = NULL;
}

void doom_app_init(void) {
    doom_started = false;
    doom_ready = false;
    doom_failed = false;
    doom_quit = false;
    frame_ready = false;
    key_read = 0;
    key_write = 0;
    doom_error[0] = '\0';
    doom_jump[0] = NULL;
}

int doom_app_tick(void) {
    if (!doom_started) {
        start_doom();
        return 1;
    }
    if (doom_failed || doom_quit) return 1;
    if (doom_ready) {
        doomgeneric_Tick();
        return 1;
    }
    return 0;
}

void doom_app_draw(int x, int y, int w, int h) {
    if (!doom_started) {
        draw_loading(x, y, w, h, "Preparing the shareware episode...");
        return;
    }
    if (doom_failed) {
        draw_loading(x, y, w, h,
                     doom_error[0] ? doom_error : "Doom could not start.");
        return;
    }
    if (doom_quit) {
        draw_loading(x, y, w, h, "Doom has ended. Press Ctrl+Q to close.");
        return;
    }
    if (!frame_ready || !DG_ScreenBuffer) {
        draw_loading(x, y, w, h, "Loading Doom...");
        return;
    }
    SYS->blit_rgb_scaled((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                         (const uint32_t *)DG_ScreenBuffer,
                         SOLIS_DOOM_FRAME_W, SOLIS_DOOM_FRAME_H);
}

void doom_app_get_size(int *w, int *h) {
    if (w) *w = 800;
    if (h) *h = 500;
}

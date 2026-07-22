#include <nyx/console.h>
#include <nyx/gdt.h>
#include <nyx/idt.h>
#include <nyx/pic.h>
#include <nyx/keyboard.h>
#include <nyx/mouse.h>
#include <nyx/graphics.h>
#include <nyx/window_manager.h>
#include <nyx/desktop.h>
#include <nyx/gui.h>
#include <nyx/timer.h>
#include <nyx/ramfs.h>
#include <nyx/apps/notepad.h>
#include <nyx/apps/terminal.h>
#include <nyx/apps/paint.h>

void __attribute__((noreturn)) kernel_panic(const char *message) {
    console_clear();
    console_set_color(0x0F, 0x04);
    console_write("Nyx OS kernel panic\n");
    console_write(message);
    console_write("\nSystem halted.\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    (void)multiboot_magic;
    (void)multiboot_info;

    console_init();
    console_set_color(0x0A, 0x00);
    console_write("Nyx OS booted.\n");
    console_write("Initializing GDT and IDT...\n");

    gdt_init();
    idt_init();

    pic_remap();
    console_write("PIC remapped.\n");

    notepad_init();
    term_init();
    paint_init();

    graphics_init(multiboot_info);
    if (graphics_is_ready()) {
        console_write("Framebuffer ready.\n");
        desktop_init();
        desktop_redraw();
    } else {
        console_write("Framebuffer unavailable.\n");
    }

    timer_init();
    ramfs_init();

    keyboard_init();
    mouse_init();

    console_write("Keyboard, mouse, timer, RAMFS ready.\n");

    __asm__ volatile("sti");

    uint32_t last_redraw = 0;

    for (;;) {
        bool mouse_moved = false;

        struct mouse_state ms;
        mouse_get_state(&ms);
        if (ms.dx || ms.dy || ms.buttons) {
            gui_handle_mouse(ms.dx, ms.dy, ms.buttons);
            if (ms.dx || ms.dy) mouse_moved = true;
        }

        if (keyboard_has_input()) {
            char key = 0;
            if (keyboard_read_char(&key)) {
                gui_handle_key(key);
            }
        }

        if (keyboard_was_start_menu_pressed()) {
            gui_toggle_start_menu();
        }

        if (gui_needs_redraw()) {
            if (mouse_moved) {
                desktop_redraw();
                last_redraw = timer_get_ticks();
            } else {
                uint32_t now = timer_get_ticks();
                if (now - last_redraw >= 1) {
                    desktop_redraw();
                    last_redraw = now;
                }
            }
        }

        __asm__ volatile("hlt");
    }
}

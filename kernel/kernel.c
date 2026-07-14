/*
 * Main kernel entry point.
 * This is the first C code executed after the boot stub hands control over.
 */

#include <myos/console.h>
#include <myos/gdt.h>
#include <myos/idt.h>
#include <myos/pic.h>
#include <myos/keyboard.h>
#include <myos/mouse.h>
#include <myos/graphics.h>

void __attribute__((noreturn)) kernel_panic(const char *message) {
    console_clear();
    console_set_color(0x0F, 0x04);
    console_write("MyOS kernel panic\n");
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
    console_write("MyOS milestone 1 booted.\n");
    console_write("Initializing GDT and IDT...\n");

    gdt_init();
    idt_init();

    pic_remap();
    console_write("PIC remapped.\n");

    graphics_init(multiboot_info);
    if (graphics_is_ready()) {
        graphics_clear(0xFF1B1B1B);
        graphics_fill_rect(40, 40, 220, 120, 0xFF4A90E2);
        graphics_draw_rect(40, 40, 220, 120, 0xFF2D5AA8);
        graphics_draw_string(60, 70, "MyOS Milestone 2", 0xFFFFFFFF);
        graphics_draw_string(60, 92, "Framebuffer active", 0xFFEDEDED);
        graphics_draw_mouse_cursor(120, 140, 0xFFFFFFFF);
    } else {
        console_write("Framebuffer unavailable.\n");
    }

    keyboard_init();
    mouse_init();

    console_write("Keyboard and mouse ready.\n");

    for (;;) {
        if (keyboard_has_input()) {
            char key = 0;
            if (keyboard_read_char(&key)) {
                console_putc(key);
            }
        }
        __asm__ volatile("hlt");
    }
}

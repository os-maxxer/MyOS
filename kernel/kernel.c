/*
 * Main kernel entry point.
 * This is the first C code executed after the boot stub hands control over.
 */

#include <myos/console.h>
#include <myos/gdt.h>
#include <myos/idt.h>

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

    console_write("Kernel ready.\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

CC ?= gcc
CFLAGS ?= -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -O2 -Iinclude
AS := gcc
ASFLAGS := -m32 -c -x assembler-with-cpp
LD ?= ld
LDFLAGS ?= -m elf_i386 -T linker.ld -nostdlib -z noexecstack

BUILD_DIR := build
BOOT_DIR := boot
KERNEL_DIR := kernel
GUI_DIR := gui
WINDOW_MANAGER_DIR := window_manager
DESKTOP_DIR := desktop
APPS_DIR := apps

OBJS := \
	$(BUILD_DIR)/boot/boot.o \
	$(BUILD_DIR)/kernel/kernel.o \
	$(BUILD_DIR)/kernel/console.o \
	$(BUILD_DIR)/kernel/gdt.o \
	$(BUILD_DIR)/kernel/idt.o \
	$(BUILD_DIR)/kernel/ports.o \
	$(BUILD_DIR)/kernel/pic.o \
	$(BUILD_DIR)/kernel/keyboard.o \
	$(BUILD_DIR)/kernel/mouse.o \
	$(BUILD_DIR)/kernel/graphics.o \
	$(BUILD_DIR)/kernel/timer.o \
	$(BUILD_DIR)/kernel/apic.o \
	$(BUILD_DIR)/kernel/ramfs.o \
	$(BUILD_DIR)/gui/gui.o \
	$(BUILD_DIR)/gui/login.o \
	$(BUILD_DIR)/window_manager/window_manager.o \
	$(BUILD_DIR)/desktop/desktop.o \
	$(BUILD_DIR)/apps/notepad.o \
	$(BUILD_DIR)/apps/terminal.o \
	$(BUILD_DIR)/apps/paint.o \
	$(BUILD_DIR)/kernel/arch/i386/interrupts.o

all: $(BUILD_DIR)/nyx.iso

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot $(BUILD_DIR)/kernel/arch/i386 $(BUILD_DIR)/gui $(BUILD_DIR)/window_manager $(BUILD_DIR)/desktop $(BUILD_DIR)/apps

$(BUILD_DIR)/boot/boot.o: $(BOOT_DIR)/boot.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/kernel.o: $(KERNEL_DIR)/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/console.o: $(KERNEL_DIR)/console.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/gdt.o: $(KERNEL_DIR)/gdt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/idt.o: $(KERNEL_DIR)/idt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/ports.o: $(KERNEL_DIR)/ports.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/pic.o: $(KERNEL_DIR)/pic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/keyboard.o: $(KERNEL_DIR)/keyboard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/mouse.o: $(KERNEL_DIR)/mouse.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/graphics.o: $(KERNEL_DIR)/graphics.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/timer.o: $(KERNEL_DIR)/timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/ramfs.o: $(KERNEL_DIR)/ramfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/apic.o: $(KERNEL_DIR)/apic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/gui.o: $(GUI_DIR)/gui.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/login.o: $(GUI_DIR)/login.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/window_manager/window_manager.o: $(WINDOW_MANAGER_DIR)/window_manager.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/desktop/desktop.o: $(DESKTOP_DIR)/desktop.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apps/notepad.o: $(APPS_DIR)/notepad.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apps/terminal.o: $(APPS_DIR)/terminal.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apps/paint.o: $(APPS_DIR)/paint.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/arch/i386/interrupts.o: $(KERNEL_DIR)/arch/i386/interrupts.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/nyx.kernel: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/nyx.iso: $(BUILD_DIR)/nyx.kernel boot/grub/grub.cfg
	mkdir -p $(BUILD_DIR)/isofiles/boot/grub
	cp $(BUILD_DIR)/nyx.kernel $(BUILD_DIR)/isofiles/boot/nyx.kernel
	cp boot/grub/grub.cfg $(BUILD_DIR)/isofiles/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD_DIR)/isofiles >/dev/null 2>&1

run: $(BUILD_DIR)/nyx.iso
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/nyx.iso

run-serial: $(BUILD_DIR)/nyx.iso
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/nyx.iso -serial file:serial.log

run-nographic: $(BUILD_DIR)/nyx.iso
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/nyx.iso -nographic

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean

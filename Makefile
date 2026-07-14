CC ?= gcc
CFLAGS ?= -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -O2 -Iinclude
AS := /usr/bin/nasm
ASFLAGS := -f elf32
LD ?= ld
LDFLAGS ?= -m elf_i386 -T linker.ld -nostdlib -z noexecstack

BUILD_DIR := build
BOOT_DIR := boot
KERNEL_DIR := kernel

OBJS := \
	$(BUILD_DIR)/boot/boot.o \
	$(BUILD_DIR)/kernel/kernel.o \
	$(BUILD_DIR)/kernel/console.o \
	$(BUILD_DIR)/kernel/gdt.o \
	$(BUILD_DIR)/kernel/idt.o \
	$(BUILD_DIR)/kernel/arch/i386/interrupts.o

all: $(BUILD_DIR)/myos.iso

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot $(BUILD_DIR)/kernel/arch/i386

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

$(BUILD_DIR)/kernel/arch/i386/interrupts.o: $(KERNEL_DIR)/arch/i386/interrupts.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/myos.kernel: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/myos.iso: $(BUILD_DIR)/myos.kernel boot/grub/grub.cfg
	mkdir -p $(BUILD_DIR)/isofiles/boot/grub
	cp $(BUILD_DIR)/myos.kernel $(BUILD_DIR)/isofiles/boot/myos.kernel
	cp boot/grub/grub.cfg $(BUILD_DIR)/isofiles/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD_DIR)/isofiles >/dev/null 2>&1

run: $(BUILD_DIR)/myos.iso
	qemu-system-i386 -cdrom $(BUILD_DIR)/myos.iso -serial stdio

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean

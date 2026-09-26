# GNU Make provides a built-in CC=cc, so ?= would leave the unavailable
# `cc` default in place on MSYS2. Keep explicit environment/command-line
# overrides, but use gcc for the project default.
ifeq ($(origin CC), default)
CC := gcc
endif
CFLAGS ?= -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-sse -mno-sse2 -mno-mmx -mno-80387 -Wall -Wextra -Werror -O2 -Iinclude
AS := gcc
ASFLAGS := -m32 -c -x assembler-with-cpp
LD ?= ld
LDFLAGS ?= -m elf_i386 -T linker.ld -nostdlib -z noexecstack
OBJCOPY ?= objcopy
GRUB_MKRESCUE := $(shell command -v grub2-mkrescue 2>/dev/null || command -v grub-mkrescue 2>/dev/null || echo grub-mkrescue)

BUILD_DIR := build
BOOT_DIR := boot
KERNEL_DIR := kernel
GUI_DIR := gui
WINDOW_MANAGER_DIR := window_manager
DESKTOP_DIR := desktop
APPS_DIR := apps
PKG_DIR := pkg
SPX_DIR := $(BUILD_DIR)/spx

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
	$(BUILD_DIR)/kernel/pointer.o \
	$(BUILD_DIR)/kernel/i2c.o \
	$(BUILD_DIR)/kernel/touchpad.o \
	$(BUILD_DIR)/kernel/graphics.o \
	$(BUILD_DIR)/kernel/timer.o \
	$(BUILD_DIR)/kernel/rtc.o \
	$(BUILD_DIR)/kernel/apic.o \
	$(BUILD_DIR)/kernel/solfs.o \
	$(BUILD_DIR)/kernel/vfs.o \
	$(BUILD_DIR)/kernel/ata.o \
	$(BUILD_DIR)/kernel/pci.o \
	$(BUILD_DIR)/kernel/rtl8139.o \
	$(BUILD_DIR)/kernel/net.o \
	$(BUILD_DIR)/kernel/acpi.o \
	$(BUILD_DIR)/kernel/dbg.o \
	$(BUILD_DIR)/kernel/spx.o \
	$(BUILD_DIR)/kernel/bmp.o \
	$(BUILD_DIR)/gui/gui.o \
	$(BUILD_DIR)/gui/login.o \
	$(BUILD_DIR)/window_manager/window_manager.o \
	$(BUILD_DIR)/desktop/desktop.o \
	$(BUILD_DIR)/kernel/arch/i386/interrupts.o

SPX_EMBED_OBJS := \
	$(SPX_DIR)/terminal_spx_embed.o \
	$(SPX_DIR)/notepad_spx_embed.o \
	$(SPX_DIR)/paint_spx_embed.o \
	$(SPX_DIR)/settings_spx_embed.o \
	$(SPX_DIR)/filebrowser_spx_embed.o \
	$(SPX_DIR)/taskmanager_spx_embed.o \
	$(SPX_DIR)/pkg_spx_embed.o \
	$(SPX_DIR)/editor_spx_embed.o \
	$(SPX_DIR)/tetris_spx_embed.o

all: $(BUILD_DIR)/solis.iso pkg-spx

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot $(BUILD_DIR)/kernel/arch/i386
	mkdir -p $(BUILD_DIR)/gui $(BUILD_DIR)/window_manager
	mkdir -p $(BUILD_DIR)/desktop $(BUILD_DIR)/apps

$(SPX_DIR):
	mkdir -p $(SPX_DIR)

# Kernel build rules
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

$(BUILD_DIR)/kernel/pointer.o: $(KERNEL_DIR)/pointer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/i2c.o: $(KERNEL_DIR)/i2c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/touchpad.o: $(KERNEL_DIR)/touchpad.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/graphics.o: $(KERNEL_DIR)/graphics.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/timer.o: $(KERNEL_DIR)/timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/rtc.o: $(KERNEL_DIR)/rtc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/solfs.o: $(KERNEL_DIR)/solfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/vfs.o: $(KERNEL_DIR)/vfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/apic.o: $(KERNEL_DIR)/apic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/ata.o: $(KERNEL_DIR)/ata.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/dbg.o: $(KERNEL_DIR)/dbg.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/spx.o: $(KERNEL_DIR)/spx.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/bmp.o: $(KERNEL_DIR)/bmp.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/pci.o: $(KERNEL_DIR)/pci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/rtl8139.o: $(KERNEL_DIR)/rtl8139.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/net.o: $(KERNEL_DIR)/net.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/acpi.o: $(KERNEL_DIR)/acpi.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/gui.o: $(GUI_DIR)/gui.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/login.o: $(GUI_DIR)/login.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/window_manager/window_manager.o: $(WINDOW_MANAGER_DIR)/window_manager.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/desktop/desktop.o: $(DESKTOP_DIR)/desktop.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/arch/i386/interrupts.o: $(KERNEL_DIR)/arch/i386/interrupts.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

# SPX app build rules
# Each app is compiled as a standalone binary, wrapped in SPX header, and embedded in the kernel

# --- Terminal ---
$(SPX_DIR)/terminal.o: $(APPS_DIR)/terminal.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/cinterp_terminal.o: $(APPS_DIR)/cinterp.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_terminal.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/terminal_spx_stub.o: $(APPS_DIR)/terminal_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/terminal.elf: $(SPX_DIR)/terminal.o $(SPX_DIR)/cinterp_terminal.o $(SPX_DIR)/glue_terminal.o $(SPX_DIR)/terminal_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01000000 -o $@ $^

$(SPX_DIR)/terminal.bin: $(SPX_DIR)/terminal.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/terminal.spx: $(SPX_DIR)/terminal.bin tools/mkspx.py
	python3 tools/mkspx.py $< Helios $@

$(SPX_DIR)/terminal_spx_embed.o: $(SPX_DIR)/terminal.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- Notepad ---
$(SPX_DIR)/notepad.o: $(APPS_DIR)/notepad.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_notepad.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/notepad_spx_stub.o: $(APPS_DIR)/notepad_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/notepad.elf: $(SPX_DIR)/notepad.o $(SPX_DIR)/glue_notepad.o $(SPX_DIR)/notepad_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01200000 -o $@ $^

$(SPX_DIR)/notepad.bin: $(SPX_DIR)/notepad.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/notepad.spx: $(SPX_DIR)/notepad.bin tools/mkspx.py
	python3 tools/mkspx.py $< Notepad $@

$(SPX_DIR)/notepad_spx_embed.o: $(SPX_DIR)/notepad.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- Paint ---
$(SPX_DIR)/paint.o: $(APPS_DIR)/paint.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_paint.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/paint_spx_stub.o: $(APPS_DIR)/paint_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/paint.elf: $(SPX_DIR)/paint.o $(SPX_DIR)/glue_paint.o $(SPX_DIR)/paint_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01400000 -o $@ $^

$(SPX_DIR)/paint.bin: $(SPX_DIR)/paint.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/paint.spx: $(SPX_DIR)/paint.bin tools/mkspx.py
	python3 tools/mkspx.py $< Paint $@

$(SPX_DIR)/paint_spx_embed.o: $(SPX_DIR)/paint.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- Settings ---
$(SPX_DIR)/settings.o: $(APPS_DIR)/settings.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_settings.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/settings_spx_stub.o: $(APPS_DIR)/settings_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/settings.elf: $(SPX_DIR)/settings.o $(SPX_DIR)/glue_settings.o $(SPX_DIR)/settings_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01600000 -o $@ $^

$(SPX_DIR)/settings.bin: $(SPX_DIR)/settings.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/settings.spx: $(SPX_DIR)/settings.bin tools/mkspx.py
	python3 tools/mkspx.py $< Settings $@

$(SPX_DIR)/settings_spx_embed.o: $(SPX_DIR)/settings.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- File Browser ---
$(SPX_DIR)/filebrowser.o: $(APPS_DIR)/filebrowser.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_filebrowser.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/filebrowser_spx_stub.o: $(APPS_DIR)/filebrowser_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/filebrowser.elf: $(SPX_DIR)/filebrowser.o $(SPX_DIR)/glue_filebrowser.o $(SPX_DIR)/filebrowser_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01800000 -o $@ $^

$(SPX_DIR)/filebrowser.bin: $(SPX_DIR)/filebrowser.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/filebrowser.spx: $(SPX_DIR)/filebrowser.bin tools/mkspx.py
	python3 tools/mkspx.py $< Files $@

$(SPX_DIR)/filebrowser_spx_embed.o: $(SPX_DIR)/filebrowser.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- Task Manager ---
$(SPX_DIR)/taskmanager.o: $(PKG_DIR)/taskmanager/taskmanager.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_taskmanager.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/taskmanager_spx_stub.o: $(PKG_DIR)/taskmanager/taskmanager_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/taskmanager.elf: $(SPX_DIR)/taskmanager.o $(SPX_DIR)/glue_taskmanager.o $(SPX_DIR)/taskmanager_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01A00000 -o $@ $^

$(SPX_DIR)/taskmanager.bin: $(SPX_DIR)/taskmanager.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/taskmanager.spx: $(SPX_DIR)/taskmanager.bin tools/mkspx.py
	python3 tools/mkspx.py $< "TaskManager" $@

$(SPX_DIR)/taskmanager_spx_embed.o: $(SPX_DIR)/taskmanager.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- SOLPKG ---
$(SPX_DIR)/pkg.o: $(PKG_DIR)/solpkg/pkg.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_pkg.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/pkg_spx_stub.o: $(PKG_DIR)/solpkg/pkg_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/pkg.elf: $(SPX_DIR)/pkg.o $(SPX_DIR)/glue_pkg.o $(SPX_DIR)/pkg_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01C00000 -o $@ $^

$(SPX_DIR)/pkg.bin: $(SPX_DIR)/pkg.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/pkg.spx: $(SPX_DIR)/pkg.bin tools/mkspx.py
	python3 tools/mkspx.py $< "SOLPKG" $@

$(SPX_DIR)/pkg_spx_embed.o: $(SPX_DIR)/pkg.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- Editor ---
$(SPX_DIR)/editor.o: $(APPS_DIR)/editor.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_editor.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/editor_spx_stub.o: $(APPS_DIR)/editor_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/editor.elf: $(SPX_DIR)/editor.o $(SPX_DIR)/glue_editor.o $(SPX_DIR)/editor_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01E00000 -o $@ $^

$(SPX_DIR)/editor.bin: $(SPX_DIR)/editor.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/editor.spx: $(SPX_DIR)/editor.bin tools/mkspx.py
	python3 tools/mkspx.py $< Editor $@

$(SPX_DIR)/editor_spx_embed.o: $(SPX_DIR)/editor.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# --- Tetris ---
$(SPX_DIR)/tetris.o: $(APPS_DIR)/tetris.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_tetris.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/tetris_spx_stub.o: $(APPS_DIR)/tetris_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/tetris.elf: $(SPX_DIR)/tetris.o $(SPX_DIR)/glue_tetris.o $(SPX_DIR)/tetris_spx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x02000000 -o $@ $^

$(SPX_DIR)/tetris.bin: $(SPX_DIR)/tetris.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/tetris.spx: $(SPX_DIR)/tetris.bin tools/mkspx.py
	python3 tools/mkspx.py $< Tetris $@

$(SPX_DIR)/tetris_spx_embed.o: $(SPX_DIR)/tetris.spx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.spx_apps $< $@

# Copy .spx files to pkg/ repo directories
$(PKG_DIR)/taskmanager/taskmanager.spx: $(SPX_DIR)/taskmanager.spx
	cp $< $@

$(PKG_DIR)/solpkg/solpkg.spx: $(SPX_DIR)/pkg.spx
	cp $< $@

pkg-spx: $(PKG_DIR)/taskmanager/taskmanager.spx $(PKG_DIR)/solpkg/solpkg.spx

# Kernel link
$(BUILD_DIR)/solis.kernel: $(OBJS) $(SPX_EMBED_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# ISO
$(BUILD_DIR)/solis.iso: $(BUILD_DIR)/solis.kernel boot/grub/grub.cfg
	mkdir -p $(BUILD_DIR)/isofiles/boot/grub
	cp $(BUILD_DIR)/solis.kernel $(BUILD_DIR)/isofiles/boot/solis.kernel
	cp boot/grub/grub.cfg $(BUILD_DIR)/isofiles/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(BUILD_DIR)/isofiles >/dev/null 2>&1

# Disk with pre-loaded packages
$(BUILD_DIR)/disk.img: pkg-spx tools/mksolfs.py
	python3 tools/mksolfs.py pkg/ $@

$(BUILD_DIR)/disk.vdi: $(BUILD_DIR)/disk.img
	VBoxManage convertfromraw $(BUILD_DIR)/disk.img $(BUILD_DIR)/disk.vdi 2>/dev/null || \
	  echo "Install VirtualBox or run: VBoxManage convertfromraw build/disk.img build/disk.vdi"

vbox-disk: $(BUILD_DIR)/disk.vdi

QEMU_VGA ?= -vga std
QEMU_DISPLAY ?= gtk,grab-on-hover=on

run: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nic user,model=rtl8139

run-x11: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	GDK_BACKEND=x11 qemu-system-i386 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nic user,model=rtl8139

run-serial: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial file:serial.log -nic user,model=rtl8139

run-x11-serial: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	GDK_BACKEND=x11 qemu-system-i386 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial file:serial.log -nic user,model=rtl8139

run-sdl: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 $(QEMU_VGA) -display sdl -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nic user,model=rtl8139

run-sdl-serial: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 $(QEMU_VGA) -display sdl -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial file:serial.log -nic user,model=rtl8139

run-nographic: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nographic -nic user,model=rtl8139

run-dbg: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial stdio -netdev user,id=net0,ip=10.0.2.15,net=10.0.2.0/24,host=10.0.2.2 -device rtl8139,netdev=net0

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run run-x11 run-sdl run-serial run-x11-serial run-sdl-serial run-nographic run-dbg clean vbox-disk

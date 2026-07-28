CC ?= gcc
CFLAGS ?= -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 -Iinclude
AS := gcc
ASFLAGS := -m32 -c -x assembler-with-cpp
LD ?= ld
LDFLAGS ?= -m elf_i386 -T linker.ld -nostdlib -z noexecstack
OBJCOPY ?= objcopy

BUILD_DIR := build
BOOT_DIR := boot
KERNEL_DIR := kernel
GUI_DIR := gui
WINDOW_MANAGER_DIR := window_manager
DESKTOP_DIR := desktop
APPS_DIR := apps
PKG_DIR := pkg
NPX_DIR := $(BUILD_DIR)/npx

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
	$(BUILD_DIR)/kernel/i2c.o \
	$(BUILD_DIR)/kernel/touchpad.o \
	$(BUILD_DIR)/kernel/graphics.o \
	$(BUILD_DIR)/kernel/timer.o \
	$(BUILD_DIR)/kernel/apic.o \
	$(BUILD_DIR)/kernel/nofs.o \
	$(BUILD_DIR)/kernel/vfs.o \
	$(BUILD_DIR)/kernel/ata.o \
	$(BUILD_DIR)/kernel/pci.o \
	$(BUILD_DIR)/kernel/rtl8139.o \
	$(BUILD_DIR)/kernel/net.o \
	$(BUILD_DIR)/kernel/dbg.o \
	$(BUILD_DIR)/kernel/npx.o \
	$(BUILD_DIR)/kernel/bmp.o \
	$(BUILD_DIR)/gui/gui.o \
	$(BUILD_DIR)/gui/login.o \
	$(BUILD_DIR)/window_manager/window_manager.o \
	$(BUILD_DIR)/desktop/desktop.o \
	$(BUILD_DIR)/kernel/arch/i386/interrupts.o

NPX_EMBED_OBJS := \
	$(NPX_DIR)/terminal_npx_embed.o \
	$(NPX_DIR)/notepad_npx_embed.o \
	$(NPX_DIR)/paint_npx_embed.o \
	$(NPX_DIR)/settings_npx_embed.o \
	$(NPX_DIR)/filebrowser_npx_embed.o \
	$(NPX_DIR)/taskmanager_npx_embed.o \
	$(NPX_DIR)/pkg_npx_embed.o

all: $(BUILD_DIR)/nyx.iso pkg-npx

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot $(BUILD_DIR)/kernel/arch/i386
	mkdir -p $(BUILD_DIR)/gui $(BUILD_DIR)/window_manager
	mkdir -p $(BUILD_DIR)/desktop $(BUILD_DIR)/apps

$(NPX_DIR):
	mkdir -p $(NPX_DIR)

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

$(BUILD_DIR)/kernel/i2c.o: $(KERNEL_DIR)/i2c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/touchpad.o: $(KERNEL_DIR)/touchpad.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/graphics.o: $(KERNEL_DIR)/graphics.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/timer.o: $(KERNEL_DIR)/timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/nofs.o: $(KERNEL_DIR)/nofs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/vfs.o: $(KERNEL_DIR)/vfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/apic.o: $(KERNEL_DIR)/apic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/ata.o: $(KERNEL_DIR)/ata.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/dbg.o: $(KERNEL_DIR)/dbg.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/npx.o: $(KERNEL_DIR)/npx.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/bmp.o: $(KERNEL_DIR)/bmp.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/pci.o: $(KERNEL_DIR)/pci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/rtl8139.o: $(KERNEL_DIR)/rtl8139.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/net.o: $(KERNEL_DIR)/net.c | $(BUILD_DIR)
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

# NPX app build rules
# Each app is compiled as a standalone binary, wrapped in NPX header, and embedded in the kernel

# --- Terminal ---
$(NPX_DIR)/terminal.o: $(APPS_DIR)/terminal.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/glue_terminal.o: $(APPS_DIR)/glue.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/terminal_npx_stub.o: $(APPS_DIR)/terminal_npx.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/terminal.elf: $(NPX_DIR)/terminal.o $(NPX_DIR)/glue_terminal.o $(NPX_DIR)/terminal_npx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01000000 -o $@ $^

$(NPX_DIR)/terminal.bin: $(NPX_DIR)/terminal.elf
	$(OBJCOPY) -O binary $< $@

$(NPX_DIR)/terminal.npx: $(NPX_DIR)/terminal.bin tools/mknpx.py
	python3 tools/mknpx.py $< Terminal $@

$(NPX_DIR)/terminal_npx_embed.o: $(NPX_DIR)/terminal.npx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.npx_apps $< $@

# --- Notepad ---
$(NPX_DIR)/notepad.o: $(APPS_DIR)/notepad.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/glue_notepad.o: $(APPS_DIR)/glue.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/notepad_npx_stub.o: $(APPS_DIR)/notepad_npx.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/notepad.elf: $(NPX_DIR)/notepad.o $(NPX_DIR)/glue_notepad.o $(NPX_DIR)/notepad_npx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01200000 -o $@ $^

$(NPX_DIR)/notepad.bin: $(NPX_DIR)/notepad.elf
	$(OBJCOPY) -O binary $< $@

$(NPX_DIR)/notepad.npx: $(NPX_DIR)/notepad.bin tools/mknpx.py
	python3 tools/mknpx.py $< Notepad $@

$(NPX_DIR)/notepad_npx_embed.o: $(NPX_DIR)/notepad.npx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.npx_apps $< $@

# --- Paint ---
$(NPX_DIR)/paint.o: $(APPS_DIR)/paint.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/glue_paint.o: $(APPS_DIR)/glue.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/paint_npx_stub.o: $(APPS_DIR)/paint_npx.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/paint.elf: $(NPX_DIR)/paint.o $(NPX_DIR)/glue_paint.o $(NPX_DIR)/paint_npx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01400000 -o $@ $^

$(NPX_DIR)/paint.bin: $(NPX_DIR)/paint.elf
	$(OBJCOPY) -O binary $< $@

$(NPX_DIR)/paint.npx: $(NPX_DIR)/paint.bin tools/mknpx.py
	python3 tools/mknpx.py $< Paint $@

$(NPX_DIR)/paint_npx_embed.o: $(NPX_DIR)/paint.npx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.npx_apps $< $@

# --- Settings ---
$(NPX_DIR)/settings.o: $(APPS_DIR)/settings.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/glue_settings.o: $(APPS_DIR)/glue.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/settings_npx_stub.o: $(APPS_DIR)/settings_npx.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/settings.elf: $(NPX_DIR)/settings.o $(NPX_DIR)/glue_settings.o $(NPX_DIR)/settings_npx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01600000 -o $@ $^

$(NPX_DIR)/settings.bin: $(NPX_DIR)/settings.elf
	$(OBJCOPY) -O binary $< $@

$(NPX_DIR)/settings.npx: $(NPX_DIR)/settings.bin tools/mknpx.py
	python3 tools/mknpx.py $< Settings $@

$(NPX_DIR)/settings_npx_embed.o: $(NPX_DIR)/settings.npx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.npx_apps $< $@

# --- File Browser ---
$(NPX_DIR)/filebrowser.o: $(APPS_DIR)/filebrowser.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/glue_filebrowser.o: $(APPS_DIR)/glue.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/filebrowser_npx_stub.o: $(APPS_DIR)/filebrowser_npx.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/filebrowser.elf: $(NPX_DIR)/filebrowser.o $(NPX_DIR)/glue_filebrowser.o $(NPX_DIR)/filebrowser_npx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01800000 -o $@ $^

$(NPX_DIR)/filebrowser.bin: $(NPX_DIR)/filebrowser.elf
	$(OBJCOPY) -O binary $< $@

$(NPX_DIR)/filebrowser.npx: $(NPX_DIR)/filebrowser.bin tools/mknpx.py
	python3 tools/mknpx.py $< Files $@

$(NPX_DIR)/filebrowser_npx_embed.o: $(NPX_DIR)/filebrowser.npx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.npx_apps $< $@

# --- Task Manager ---
$(NPX_DIR)/taskmanager.o: $(PKG_DIR)/taskmanager/taskmanager.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/glue_taskmanager.o: $(APPS_DIR)/glue.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/taskmanager_npx_stub.o: $(PKG_DIR)/taskmanager/taskmanager_npx.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/taskmanager.elf: $(NPX_DIR)/taskmanager.o $(NPX_DIR)/glue_taskmanager.o $(NPX_DIR)/taskmanager_npx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01A00000 -o $@ $^

$(NPX_DIR)/taskmanager.bin: $(NPX_DIR)/taskmanager.elf
	$(OBJCOPY) -O binary $< $@

$(NPX_DIR)/taskmanager.npx: $(NPX_DIR)/taskmanager.bin tools/mknpx.py
	python3 tools/mknpx.py $< "TaskManager" $@

$(NPX_DIR)/taskmanager_npx_embed.o: $(NPX_DIR)/taskmanager.npx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.npx_apps $< $@

# --- NYPKG ---
$(NPX_DIR)/pkg.o: $(PKG_DIR)/nypkg/pkg.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/glue_pkg.o: $(APPS_DIR)/glue.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/pkg_npx_stub.o: $(PKG_DIR)/nypkg/pkg_npx.c | $(NPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(NPX_DIR)/pkg.elf: $(NPX_DIR)/pkg.o $(NPX_DIR)/glue_pkg.o $(NPX_DIR)/pkg_npx_stub.o
	$(LD) -m elf_i386 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01C00000 -o $@ $^

$(NPX_DIR)/pkg.bin: $(NPX_DIR)/pkg.elf
	$(OBJCOPY) -O binary $< $@

$(NPX_DIR)/pkg.npx: $(NPX_DIR)/pkg.bin tools/mknpx.py
	python3 tools/mknpx.py $< "NYPKG" $@

$(NPX_DIR)/pkg_npx_embed.o: $(NPX_DIR)/pkg.npx
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 --rename-section .data=.npx_apps $< $@

# Copy .npx files to pkg/ repo directories
$(PKG_DIR)/taskmanager/taskmanager.npx: $(NPX_DIR)/taskmanager.npx
	cp $< $@

$(PKG_DIR)/nypkg/nypkg.npx: $(NPX_DIR)/pkg.npx
	cp $< $@

pkg-npx: $(PKG_DIR)/taskmanager/taskmanager.npx $(PKG_DIR)/nypkg/nypkg.npx

# Kernel link
$(BUILD_DIR)/nyx.kernel: $(OBJS) $(NPX_EMBED_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# ISO
$(BUILD_DIR)/nyx.iso: $(BUILD_DIR)/nyx.kernel boot/grub/grub.cfg
	mkdir -p $(BUILD_DIR)/isofiles/boot/grub
	cp $(BUILD_DIR)/nyx.kernel $(BUILD_DIR)/isofiles/boot/nyx.kernel
	cp boot/grub/grub.cfg $(BUILD_DIR)/isofiles/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD_DIR)/isofiles >/dev/null 2>&1

# Disk with pre-loaded packages
$(BUILD_DIR)/disk.img: pkg-npx
	python3 tools/mknofs.py pkg/ $@

$(BUILD_DIR)/disk.vdi: $(BUILD_DIR)/disk.img
	VBoxManage convertfromraw $(BUILD_DIR)/disk.img $(BUILD_DIR)/disk.vdi 2>/dev/null || \
	  echo "Install VirtualBox or run: VBoxManage convertfromraw build/disk.img build/disk.vdi"

vbox-disk: $(BUILD_DIR)/disk.vdi

run: $(BUILD_DIR)/nyx.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/nyx.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nic user,model=rtl8139

run-serial: $(BUILD_DIR)/nyx.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/nyx.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial file:serial.log -nic user,model=rtl8139

run-nographic: $(BUILD_DIR)/nyx.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/nyx.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nographic -nic user,model=rtl8139

run-dbg: $(BUILD_DIR)/nyx.iso $(BUILD_DIR)/disk.img
	qemu-system-i386 -vga std -m 256M -cdrom $(BUILD_DIR)/nyx.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial stdio -netdev user,id=net0,ip=10.0.2.15,net=10.0.2.0/24,host=10.0.2.2 -device rtl8139,netdev=net0

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean vbox-disk

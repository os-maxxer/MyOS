# GNU Make provides a built-in CC=cc, so ?= would leave the unavailable
# `cc` default in place on MSYS2. Keep explicit environment/command-line
# overrides, but use gcc for the project default.
ifeq ($(origin CC), default)
CC := gcc
endif
CFLAGS ?= -m64 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 -Wall -Wextra -Werror -O2 -Iinclude
AS := gcc
ASFLAGS := -m64 -c -x assembler-with-cpp
LD ?= ld
LDFLAGS ?= -m elf_x86_64 -T linker.ld -nostdlib -z noexecstack
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
DOOM_DIR := third_party/doom
DOOM_SRCS := $(wildcard $(DOOM_DIR)/*.c) $(wildcard $(DOOM_DIR)/solis/*.c)
DOOM_OBJS := $(patsubst $(DOOM_DIR)/%.c,$(BUILD_DIR)/doom/%.o,$(DOOM_SRCS))
DOOM_CFLAGS := -m64 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
	-fno-asynchronous-unwind-tables -mno-red-zone -msse2 -mfpmath=sse -O2 -w \
	-DSOLIS -Iinclude -I$(DOOM_DIR) -I$(DOOM_DIR)/compat -I$(DOOM_DIR)/solis

OBJS := \
	$(BUILD_DIR)/boot/boot.o \
	$(BUILD_DIR)/kernel/kernel.o \
	$(BUILD_DIR)/kernel/pmm.o \
	$(BUILD_DIR)/kernel/vmm.o \
	$(BUILD_DIR)/kernel/user_elf.o \
	$(BUILD_DIR)/kernel/task.o \
	$(BUILD_DIR)/kernel/usermode.o \
	$(BUILD_DIR)/kernel/console.o \
	$(BUILD_DIR)/kernel/gdt.o \
	$(BUILD_DIR)/kernel/tss.o \
	$(BUILD_DIR)/kernel/idt.o \
	$(BUILD_DIR)/kernel/ports.o \
	$(BUILD_DIR)/kernel/pic.o \
	$(BUILD_DIR)/kernel/keyboard.o \
	$(BUILD_DIR)/kernel/mouse.o \
	$(BUILD_DIR)/kernel/pointer.o \
	$(BUILD_DIR)/kernel/usb.o \
	$(BUILD_DIR)/kernel/i2c.o \
	$(BUILD_DIR)/kernel/touchpad.o \
	$(BUILD_DIR)/kernel/graphics.o \
	$(BUILD_DIR)/kernel/timer.o \
	$(BUILD_DIR)/kernel/rtc.o \
	$(BUILD_DIR)/kernel/apic.o \
	$(BUILD_DIR)/kernel/solfs.o \
	$(BUILD_DIR)/kernel/vfs.o \
	$(BUILD_DIR)/kernel/setup.o \
	$(BUILD_DIR)/kernel/ata.o \
	$(BUILD_DIR)/kernel/ahci.o \
	$(BUILD_DIR)/kernel/pci.o \
	$(BUILD_DIR)/kernel/rtl8139.o \
	$(BUILD_DIR)/kernel/net_device.o \
	$(BUILD_DIR)/kernel/net.o \
	$(BUILD_DIR)/kernel/acpi.o \
	$(BUILD_DIR)/kernel/dbg.o \
	$(BUILD_DIR)/kernel/jit.o \
	$(BUILD_DIR)/kernel/spx.o \
	$(BUILD_DIR)/kernel/bmp.o \
	$(BUILD_DIR)/kernel/doom_wad.o \
	$(BUILD_DIR)/gui/gui.o \
	$(BUILD_DIR)/gui/solis_ui_engine.o \
	$(BUILD_DIR)/gui/login.o \
	$(BUILD_DIR)/gui/setup_wizard.o \
	$(BUILD_DIR)/gui/boot.o \
	$(BUILD_DIR)/window_manager/window_manager.o \
	$(BUILD_DIR)/desktop/desktop.o \
	$(BUILD_DIR)/kernel/arch/x86_64/interrupts.o \
	$(BUILD_DIR)/kernel/arch/x86_64/usermode_entry.o \
	$(BUILD_DIR)/user_test_embed.o

SPX_EMBED_OBJS := \
	$(SPX_DIR)/terminal_spx_embed.o \
	$(SPX_DIR)/notepad_spx_embed.o \
	$(SPX_DIR)/paint_spx_embed.o \
	$(SPX_DIR)/settings_spx_embed.o \
	$(SPX_DIR)/filebrowser_spx_embed.o \
	$(SPX_DIR)/taskmanager_spx_embed.o \
	$(SPX_DIR)/pkg_spx_embed.o \
	$(SPX_DIR)/editor_spx_embed.o \
	$(SPX_DIR)/tetris_spx_embed.o \
	$(SPX_DIR)/blocks_spx_embed.o \
	$(SPX_DIR)/calculator_spx_embed.o

SPX_EMBED_OBJS += $(SPX_DIR)/doom_spx_embed.o

all: $(BUILD_DIR)/solis.iso pkg-spx

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot $(BUILD_DIR)/kernel/arch/x86_64
	mkdir -p $(BUILD_DIR)/gui $(BUILD_DIR)/window_manager
	mkdir -p $(BUILD_DIR)/desktop $(BUILD_DIR)/apps
	mkdir -p $(BUILD_DIR)/doom/solis

$(SPX_DIR):
	mkdir -p $(SPX_DIR)

# Kernel build rules
$(BUILD_DIR)/boot/boot.o: $(BOOT_DIR)/boot.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/kernel.o: $(KERNEL_DIR)/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/pmm.o: $(KERNEL_DIR)/pmm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/vmm.o: $(KERNEL_DIR)/vmm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/usermode.o: $(KERNEL_DIR)/usermode.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/user_elf.o: $(KERNEL_DIR)/user_elf.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/task.o: $(KERNEL_DIR)/task.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/console.o: $(KERNEL_DIR)/console.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/gdt.o: $(KERNEL_DIR)/gdt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/tss.o: $(KERNEL_DIR)/tss.c | $(BUILD_DIR)
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

$(BUILD_DIR)/kernel/usb.o: $(KERNEL_DIR)/usb.c | $(BUILD_DIR)
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

$(BUILD_DIR)/kernel/setup.o: $(KERNEL_DIR)/setup.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/apic.o: $(KERNEL_DIR)/apic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/ata.o: $(KERNEL_DIR)/ata.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/ahci.o: $(KERNEL_DIR)/ahci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/dbg.o: $(KERNEL_DIR)/dbg.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/jit.o: $(KERNEL_DIR)/jit.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/spx.o: $(KERNEL_DIR)/spx.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/bmp.o: $(KERNEL_DIR)/bmp.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/doom_wad.o: $(KERNEL_DIR)/doom_wad.S $(DOOM_DIR)/doom1.wad | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/kernel/pci.o: $(KERNEL_DIR)/pci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/rtl8139.o: $(KERNEL_DIR)/rtl8139.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/net_device.o: $(KERNEL_DIR)/net_device.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/net.o: $(KERNEL_DIR)/net.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/acpi.o: $(KERNEL_DIR)/acpi.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/gui.o: $(GUI_DIR)/gui.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/solis_ui_engine.o: $(GUI_DIR)/solis_ui_engine.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/login.o: $(GUI_DIR)/login.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/setup_wizard.o: $(GUI_DIR)/setup_wizard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gui/boot.o: $(GUI_DIR)/boot.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/window_manager/window_manager.o: $(WINDOW_MANAGER_DIR)/window_manager.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/desktop/desktop.o: $(DESKTOP_DIR)/desktop.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/arch/x86_64/interrupts.o: $(KERNEL_DIR)/arch/x86_64/interrupts.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/kernel/arch/x86_64/usermode_entry.o: $(KERNEL_DIR)/arch/x86_64/usermode.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

# Tiny freestanding C user program used to exercise ring-3 syscalls and
# timer-driven preemption. It deliberately avoids libc and compiler runtime.
$(BUILD_DIR)/user_test.o: tests/user/user_test.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -mcmodel=large -c $< -o $@

$(BUILD_DIR)/user_start.o: tests/user/user_start.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/user_test.elf: $(BUILD_DIR)/user_test.o $(BUILD_DIR)/user_start.o tests/user/user_test.ld
	$(LD) -m elf_x86_64 -T tests/user/user_test.ld -nostdlib -z noexecstack -o $@ $(BUILD_DIR)/user_start.o $(BUILD_DIR)/user_test.o

$(BUILD_DIR)/user_test_embed.o: $(BUILD_DIR)/user_test.elf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

# SPX app build rules
# Each app is compiled as a standalone binary, wrapped in SPX header, and embedded in the kernel

# --- Terminal ---
$(SPX_DIR)/terminal.o: $(APPS_DIR)/terminal.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/ccompiler_terminal.o: $(APPS_DIR)/ccompiler.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/lua_terminal.o: $(APPS_DIR)/lua.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_terminal.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/terminal_spx_stub.o: $(APPS_DIR)/terminal_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/terminal.elf: $(SPX_DIR)/terminal.o $(SPX_DIR)/ccompiler_terminal.o $(SPX_DIR)/lua_terminal.o $(SPX_DIR)/glue_terminal.o $(SPX_DIR)/terminal_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01000000 -o $@ $^

$(SPX_DIR)/terminal.bin: $(SPX_DIR)/terminal.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/terminal.spx: $(SPX_DIR)/terminal.bin tools/mkspx.py
	python3 tools/mkspx.py $< Helios $@

$(SPX_DIR)/terminal_spx_embed.o: $(SPX_DIR)/terminal.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Notepad ---
$(SPX_DIR)/notepad.o: $(APPS_DIR)/notepad.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_notepad.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/notepad_spx_stub.o: $(APPS_DIR)/notepad_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/notepad.elf: $(SPX_DIR)/notepad.o $(SPX_DIR)/glue_notepad.o $(SPX_DIR)/notepad_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01200000 -o $@ $^

$(SPX_DIR)/notepad.bin: $(SPX_DIR)/notepad.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/notepad.spx: $(SPX_DIR)/notepad.bin tools/mkspx.py
	python3 tools/mkspx.py $< Notepad $@

$(SPX_DIR)/notepad_spx_embed.o: $(SPX_DIR)/notepad.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Paint ---
$(SPX_DIR)/paint.o: $(APPS_DIR)/paint.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_paint.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/paint_spx_stub.o: $(APPS_DIR)/paint_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/paint.elf: $(SPX_DIR)/paint.o $(SPX_DIR)/glue_paint.o $(SPX_DIR)/paint_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01400000 -o $@ $^

$(SPX_DIR)/paint.bin: $(SPX_DIR)/paint.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/paint.spx: $(SPX_DIR)/paint.bin tools/mkspx.py
	python3 tools/mkspx.py $< Paint $@

$(SPX_DIR)/paint_spx_embed.o: $(SPX_DIR)/paint.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Settings ---
$(SPX_DIR)/settings.o: $(APPS_DIR)/settings.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_settings.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/settings_spx_stub.o: $(APPS_DIR)/settings_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/settings.elf: $(SPX_DIR)/settings.o $(SPX_DIR)/glue_settings.o $(SPX_DIR)/settings_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01600000 -o $@ $^

$(SPX_DIR)/settings.bin: $(SPX_DIR)/settings.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/settings.spx: $(SPX_DIR)/settings.bin tools/mkspx.py
	python3 tools/mkspx.py $< Settings $@

$(SPX_DIR)/settings_spx_embed.o: $(SPX_DIR)/settings.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- File Browser ---
$(SPX_DIR)/filebrowser.o: $(APPS_DIR)/filebrowser.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_filebrowser.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/filebrowser_spx_stub.o: $(APPS_DIR)/filebrowser_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/filebrowser.elf: $(SPX_DIR)/filebrowser.o $(SPX_DIR)/glue_filebrowser.o $(SPX_DIR)/filebrowser_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01800000 -o $@ $^

$(SPX_DIR)/filebrowser.bin: $(SPX_DIR)/filebrowser.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/filebrowser.spx: $(SPX_DIR)/filebrowser.bin tools/mkspx.py
	python3 tools/mkspx.py $< Files $@

$(SPX_DIR)/filebrowser_spx_embed.o: $(SPX_DIR)/filebrowser.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Task Manager ---
$(SPX_DIR)/taskmanager.o: $(PKG_DIR)/taskmanager/taskmanager.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_taskmanager.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/taskmanager_spx_stub.o: $(PKG_DIR)/taskmanager/taskmanager_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/taskmanager.elf: $(SPX_DIR)/taskmanager.o $(SPX_DIR)/glue_taskmanager.o $(SPX_DIR)/taskmanager_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01A00000 -o $@ $^

$(SPX_DIR)/taskmanager.bin: $(SPX_DIR)/taskmanager.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/taskmanager.spx: $(SPX_DIR)/taskmanager.bin tools/mkspx.py
	python3 tools/mkspx.py $< "TaskManager" $@

$(SPX_DIR)/taskmanager_spx_embed.o: $(SPX_DIR)/taskmanager.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- SOLPKG ---
$(SPX_DIR)/pkg.o: $(PKG_DIR)/solpkg/pkg.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_pkg.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/pkg_spx_stub.o: $(PKG_DIR)/solpkg/pkg_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/pkg.elf: $(SPX_DIR)/pkg.o $(SPX_DIR)/glue_pkg.o $(SPX_DIR)/pkg_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01C00000 -o $@ $^

$(SPX_DIR)/pkg.bin: $(SPX_DIR)/pkg.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/pkg.spx: $(SPX_DIR)/pkg.bin tools/mkspx.py
	python3 tools/mkspx.py $< "SOLPKG" $@

$(SPX_DIR)/pkg_spx_embed.o: $(SPX_DIR)/pkg.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Editor ---
$(SPX_DIR)/editor.o: $(APPS_DIR)/editor.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_editor.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/editor_spx_stub.o: $(APPS_DIR)/editor_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/editor.elf: $(SPX_DIR)/editor.o $(SPX_DIR)/glue_editor.o $(SPX_DIR)/editor_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x01E00000 -o $@ $^

$(SPX_DIR)/editor.bin: $(SPX_DIR)/editor.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/editor.spx: $(SPX_DIR)/editor.bin tools/mkspx.py
	python3 tools/mkspx.py $< Editor $@

$(SPX_DIR)/editor_spx_embed.o: $(SPX_DIR)/editor.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Tetris ---
$(SPX_DIR)/tetris.o: $(APPS_DIR)/tetris.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_tetris.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/tetris_spx_stub.o: $(APPS_DIR)/tetris_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/tetris.elf: $(SPX_DIR)/tetris.o $(SPX_DIR)/glue_tetris.o $(SPX_DIR)/tetris_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x02000000 -o $@ $^

$(SPX_DIR)/tetris.bin: $(SPX_DIR)/tetris.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/tetris.spx: $(SPX_DIR)/tetris.bin tools/mkspx.py
	python3 tools/mkspx.py $< Tetris $@

$(SPX_DIR)/tetris_spx_embed.o: $(SPX_DIR)/tetris.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Solis Blocks ---
$(SPX_DIR)/blocks.o: $(APPS_DIR)/blocks.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_blocks.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/blocks_spx_stub.o: $(APPS_DIR)/blocks_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/blocks.elf: $(SPX_DIR)/blocks.o $(SPX_DIR)/glue_blocks.o $(SPX_DIR)/blocks_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x02200000 -o $@ $^

$(SPX_DIR)/blocks.bin: $(SPX_DIR)/blocks.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/blocks.spx: $(SPX_DIR)/blocks.bin tools/mkspx.py
	python3 tools/mkspx.py $< Blocks $@

$(SPX_DIR)/blocks_spx_embed.o: $(SPX_DIR)/blocks.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Calculator ---
$(SPX_DIR)/calculator.o: $(APPS_DIR)/calculator.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/glue_calculator.o: $(APPS_DIR)/glue.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/calculator_spx_stub.o: $(APPS_DIR)/calculator_spx.c | $(SPX_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SPX_DIR)/calculator.elf: $(SPX_DIR)/calculator.o $(SPX_DIR)/glue_calculator.o $(SPX_DIR)/calculator_spx_stub.o
	$(LD) -m elf_x86_64 -T $(APPS_DIR)/link_app.ld -nostdlib --defsym BASE=0x02400000 -o $@ $^

$(SPX_DIR)/calculator.bin: $(SPX_DIR)/calculator.elf
	$(OBJCOPY) -O binary $< $@

$(SPX_DIR)/calculator.spx: $(SPX_DIR)/calculator.bin tools/mkspx.py
	python3 tools/mkspx.py $< Calculator $@

$(SPX_DIR)/calculator_spx_embed.o: $(SPX_DIR)/calculator.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# --- Doom (Extra edition) ---
# Doom is a normal SPX app with its own private heap at 64 MiB. Its WAD is
# embedded once in the kernel and exposed through the syscall table, so it is
# not duplicated in every app or in the filesystem image.
$(BUILD_DIR)/doom/%.o: $(DOOM_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

$(SPX_DIR)/doom.elf: $(DOOM_OBJS) apps/link_app.ld
	$(LD) -m elf_x86_64 -T apps/link_app.ld -nostdlib --defsym BASE=0x02600000 -o $@ $(DOOM_OBJS)

$(SPX_DIR)/doom.bin: $(SPX_DIR)/doom.elf
	$(OBJCOPY) --set-section-flags .bss=alloc,load,contents -O binary $< $@

$(SPX_DIR)/doom.spx: $(SPX_DIR)/doom.bin tools/mkspx.py
	python3 tools/mkspx.py $< Doom $@

$(SPX_DIR)/doom_spx_embed.o: $(SPX_DIR)/doom.spx
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.spx_apps $< $@

# Copy .spx files to pkg/ repo directories
$(PKG_DIR)/taskmanager/taskmanager.spx: $(SPX_DIR)/taskmanager.spx
	cp $< $@

$(PKG_DIR)/solpkg/solpkg.spx: $(SPX_DIR)/pkg.spx
	cp $< $@

pkg-spx: $(PKG_DIR)/taskmanager/taskmanager.spx $(PKG_DIR)/solpkg/solpkg.spx

# Install the host packages needed to build and run Solis.
get:
	@printf 'What distro are you on? '; \
	read distro; \
	distro=$$(printf '%s' "$$distro" | tr '[:upper:]' '[:lower:]'); \
	case "$$distro" in \
		debian|ubuntu|linux\ mint|linuxmint|pop\ os|pop_os) \
			sudo apt-get update && sudo apt-get install -y build-essential binutils grub-pc-bin grub-efi-amd64-bin gdisk efibootmgr xorriso mtools qemu-system-x86 python3 ;; \
		fedora) \
			sudo dnf install -y gcc make binutils grub2-tools grub2-efi-x64-modules gdisk efibootmgr xorriso mtools qemu-system-x86 python3 ;; \
		arch|arch\ linux|manjaro|endeavouros) \
			sudo pacman -S --needed --noconfirm base-devel binutils grub gptfdisk efibootmgr xorriso mtools qemu-system-x86 python ;; \
		opensuse|opensuse\ leap|opensuse\ tumbleweed) \
			sudo zypper install -y gcc make binutils grub2-tools grub2-x86_64-efi gdisk efibootmgr xorriso mtools qemu-x86 python3 ;; \
		*) \
			printf "Unsupported distro: %s\\nSupported: Debian/Ubuntu, Fedora, Arch, openSUSE.\\n" "$$distro"; exit 1 ;; \
	esac

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
$(BUILD_DIR)/disk.img: pkg-spx $(BUILD_DIR)/user_test.elf tools/mksolfs.py
	python3 tools/mksolfs.py pkg/ $@ $(BUILD_DIR)/user_test.elf

$(BUILD_DIR)/solis.vdi: $(BUILD_DIR)/disk.img
	@if command -v VBoxManage >/dev/null 2>&1; then \
		VBoxManage convertfromraw $(BUILD_DIR)/disk.img $@; \
	elif command -v qemu-img >/dev/null 2>&1; then \
		qemu-img convert -f raw -O vdi $(BUILD_DIR)/disk.img $@; \
	else \
		echo "Install VirtualBox (VBoxManage) or QEMU (qemu-img) to create build/solis.vdi."; \
		exit 1; \
	fi

vbox-disk: $(BUILD_DIR)/solis.vdi

# A blank disk for testing first-boot formatting. Attach this instead of
# build/disk.img in VirtualBox; Solis Setup formats it on install.
blank-vdi:
	@bash tools/make-blank-vdi.sh

# A self-booting UEFI disk: FAT32 ESP (GRUB + kernel) plus a SolFS partition.
# No ISO is needed when the VM is configured for EFI firmware.
$(BUILD_DIR)/solis-uefi.vdi: $(BUILD_DIR)/solis.kernel $(BUILD_DIR)/user_test.elf pkg-spx \
		tools/make-bootable-vdi.sh boot/grub/grub-uefi.cfg
	bash tools/make-bootable-vdi.sh

boot-vdi: $(BUILD_DIR)/solis-uefi.vdi

QEMU_VGA ?= -vga std
QEMU_DISPLAY ?= gtk,grab-on-hover=on

run: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-x86_64 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nic user,model=rtl8139

run-usb: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-x86_64 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0 -nic user,model=rtl8139

run-x11: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	GDK_BACKEND=x11 qemu-system-x86_64 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nic user,model=rtl8139

run-serial: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-x86_64 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial file:serial.log -nic user,model=rtl8139

run-x11-serial: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	GDK_BACKEND=x11 qemu-system-x86_64 $(QEMU_VGA) -display $(QEMU_DISPLAY) -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial file:serial.log -nic user,model=rtl8139

run-sdl: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-x86_64 $(QEMU_VGA) -display sdl -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nic user,model=rtl8139

run-sdl-serial: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-x86_64 $(QEMU_VGA) -display sdl -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial file:serial.log -nic user,model=rtl8139

run-nographic: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-x86_64 -vga std -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -nographic -nic user,model=rtl8139

run-dbg: $(BUILD_DIR)/solis.iso $(BUILD_DIR)/disk.img
	qemu-system-x86_64 -vga std -m 256M -cdrom $(BUILD_DIR)/solis.iso -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide -serial stdio -netdev user,id=net0,ip=10.0.2.15,net=10.0.2.0/24,host=10.0.2.2 -device rtl8139,netdev=net0

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all get run run-x11 run-sdl run-serial run-x11-serial run-sdl-serial run-nographic run-dbg clean vbox-disk blank-vdi boot-vdi

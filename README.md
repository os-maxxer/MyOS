# Solis OS

Solis is a 32-bit hobby operating system for x86 (i386). It boots with GRUB
(Multiboot2), draws a GUI desktop through a VBE framebuffer, and ships a small
set of built-in applications. It is written in freestanding C with no libc, and
is primarily developed and run inside QEMU.

## Stability status

Solis is **partially stable**. It boots reliably and the desktop, apps, and
filesystem work in everyday use, but it is a hobby OS that has **not been
tested to its full extent**:

- There is **no installer yet** — Solis currently runs from the ISO/disk
  images built by `make` inside an emulator (QEMU) or VM.
- It has not been validated on a wide range of real hardware.
- Expect occasional bugs and rough edges.

## Features

- **Boot**: GRUB 2 / Multiboot2, 1024x768x32 VBE framebuffer
- **GUI**: Desktop, window manager, top bar with a live clock, dock, and app grid
- **Themes**: Gnome, Sunset, Cherry Blossom, Starfield, and solid colors
- **Apps**: Terminal, Notepad, Paint, Settings, Files, Task Manager, SOLPKG, Editor, Tetris
- **Filesystem**: SOLFS on an ATA (IDE) disk, plus a small VFS
- **Networking**: RTL8139 driver with ARP, ping, DNS, and HTTP client support
- **Input**: PS/2 keyboard + mouse, and an I2C touchpad driver
- **Time**: CMOS real-time clock driver with a selectable timezone and a
  syscall API usable by applications (see below)

## Applications

Solis ships with nine built-in apps. They are compiled as freestanding
binaries, wrapped in the SPX package format, and embedded in the kernel. They
can be launched from the desktop icons, the bottom dock, or the Apps grid
(launcher button on the left of the top bar):

| App          | What it does                                                        |
|--------------|---------------------------------------------------------------------|
| **Terminal** | Interactive shell with a small set of built-in commands             |
| **Notepad**  | Plain-text editor with save/load                                    |
| **Paint**    | Pixel drawing canvas with a palette and brush tools                 |
| **Settings** | Theme/wallpaper, background color, and timezone configuration        |
| **Files**    | Browse the SOLFS disk                                                |
| **Task Manager** | Live CPU load graph and process list                            |
| **SOLPKG**   | Package manager for the SOLPKG repo                                  |
| **Editor**   | Source code editor with C syntax highlighting (Ctrl+S save, Ctrl+N new) |
| **Tetris**   | Classic falling-blocks game                                          |

## Using the desktop

- Click a **desktop icon**, a **dock** entry, or open the **Apps grid** to
  launch an app. Windows can be dragged by their title bar.
- Each window has **minimize**, **maximize**, and **close** buttons (top
  right) with hover and press feedback; windows cast a soft drop shadow.
- The **top bar** shows the launcher, open window names, and a live clock.
- The **Settings** app changes the theme and background in real time.

## Minimum system requirements

Solis is a lightweight 32-bit OS, but it still expects a specific hardware
environment. These are the minimums to run it (the defaults used by the
`make run` QEMU setup):

- **CPU**: 32-bit x86 (i386 or newer); one core is enough
- **RAM**: 256 MB
- **Graphics**: VBE-capable display adapter able to provide a
  1024x768x32 framebuffer mode
- **Storage**: an ATA (IDE) disk for the SOLFS file system
- **Input**: a PS/2 keyboard and PS/2 mouse (I2C touchpad is optional)
- **Network** *(optional)*: an RTL8139 Ethernet card for networking
- **Boot**: GRUB 2 with Multiboot2 support

The recommended way to run Solis today is inside QEMU (see
[Running](#running)) — no installer is required there.

## Requirements (for building)

The build targets i386 (`-m32`) and needs the usual OS-dev toolchain. On
Fedora, install the following (VirtualBox is optional, only for `make vbox-disk`):

```sh
sudo dnf install -y \
  gcc glibc-devel.i686 libgcc.i686 make \
  qemu-system-x86 grub2-tools xorriso mtools
```

Other distros: `gcc-multilib` + `qemu-system-i386` + `grub-pc-bin` +
`xorriso` + `mtools` (Debian/Ubuntu) or the equivalent `-m32` capable toolchain.

## Building

```sh
make            # builds build/solis.iso and the package-disk SPX files
make build/disk.img  # also build the SOLFS disk image (built automatically by make run)
make clean      # removes build artifacts
```

The kernel and every application are compiled into a single ISO, and the nine
built-in apps are embedded in the kernel as SPX packages.

## Running

Run in QEMU (default VGA):

```sh
make run
```

Other targets:

```sh
make run-serial      # logs kernel serial output to serial.log
make run-nographic   # headless (serial console)
make run-dbg         # serial to stdio with a user-mode NIC
make vbox-disk       # convert build/disk.img to build/disk.vdi for VirtualBox
```

The default QEMU invocation boots the ISO with the pre-loaded package disk
attached as an IDE drive. Networking uses QEMU's user-mode NIC with an RTL8139
model (`-nic user,model=rtl8139`).

## Clock & timezones

Solis reads the date/time from the CMOS real-time clock and treats it as UTC,
then applies the selected timezone offset. The current time is shown live in
the top bar. To change the timezone:

1. Open **Settings** (desktop icon, dock, or the Apps grid).
2. Go to the **Time & Date** tab.
3. Click a timezone (e.g. *Chicago*, *New York*, *London*, *Tokyo*).

The selection applies immediately to the whole system. It is kept in memory,
so it resets to UTC on reboot.

### RTC API

The time API is exposed to applications through the syscall table (see
`include/solis/syscall.h`). Kernel side lives in `kernel/rtc.c` and apps reach
it through the wrappers in `apps/glue.c`.

```c
#include <solis/rtc.h>

struct rtc_time t;
rtc_get_time(&t);                 /* fills t with local time + date        */
rtc_set_timezone(3);              /* switch to Chicago (UTC-6)             */
int  tz = rtc_get_timezone();     /* index of the active timezone          */
int  n  = rtc_get_timezone_count();
rtc_get_timezone_name(i, buf, n); /* copy a timezone's name into buf       */
int  off = rtc_get_timezone_offset(i); /* minutes east of UTC for zone i   */
```

`struct rtc_time` fields: `second`, `minute`, `hour`, `day`, `month`, `year`,
`weekday` (0 = Sunday .. 6 = Saturday), `tz_offset` (minutes east of UTC) and
`tz_index` (selected timezone index). Timezone offsets are fixed; daylight
saving is not applied.

## Project layout

```
boot/             boot.S + GRUB config
kernel/           core kernel: console, gdt/idt/pic, keyboard/mouse, RTC,
                  graphics (VBE), timer (PIT), ATA, SOLFS/VFS, SPX package
                  loader, PCI + RTL8139 networking
gui/              desktop shell (windows, top bar, dock, app grid, login)
window_manager/   thin facade over the GUI
desktop/          desktop init/redraw
apps/             built-in SPX applications + syscall glue
pkg/              packaged apps (taskmanager, solpkg) and the SOLPKG repo
                  metadata
tools/            build helpers (mkspx.py, mksolfs.py) and a test HTTP server
include/solis/    public headers shared by the kernel and applications
```

## License

[Apache 2.0](LICENSE)

# Solis OS: achievements and next steps

This is a handoff note for the next person or AI agent continuing work on
Solis OS. It records verified architecture work, the project toolchain, current
limitations, and a practical order for the next milestones. The main
[README](./README.md) remains the user-facing feature and build guide.

## Current state

Solis boots as an x86-64 kernel through GRUB/Multiboot2 and reaches a graphical
desktop. It is still an experimental hobby OS, not a Linux-equivalent general
purpose system. Desktop SPX applications execute in kernel mode; the user-mode
work below is a boot-time demonstration, not the runtime model for desktop
apps.

The user's comparison to Linux distributions' historical shift to 64-bit is an
informal maturity estimate, not a measurable percentage. Before the ELF loader
work in this handoff, progress was estimated around 15-20%. The loader is a
meaningful step, but do not claim the 35% target has been reached: scheduling,
process lifetime, general system calls, and user-mode applications are still
missing.

## Achievements

- The 32-bit Multiboot2 entry checks for PAE and long-mode support, enables
  x86-64 paging, and enters the 64-bit kernel.
- The kernel has a Multiboot memory-map-based physical page allocator and
  four-level virtual memory. Kernel text is read-only; data is non-executable
  when NX is supported.
- The x86-64 GDT has a TSS, a ring-0 stack, and dedicated IST stacks for
  double-fault, NMI, and machine-check handling. Exceptions report useful
  register and page-fault details.
- Separate user page-table roots keep user mappings apart and retain
  supervisor-only kernel mappings.
- Ring-3 entry/return and a minimal `int 0x80` ABI exist. `write` copies and
  validates user memory; `exit` returns control to the kernel.
- A small static ELF64 loader validates ELF identity, architecture, headers,
  segment ranges, file bounds, and executable entry points. It loads user
  `PT_LOAD` segments into separate address spaces with writable/executable
  permissions derived from segment flags and NX applied to non-executable
  segments where supported. Dynamic/interpreted ELF files and writable+
  executable segments are rejected.
- The boot self-test loads a separately linked ELF64 program, enters ring 3,
  checks that a kernel-space pointer is rejected by `write`, writes a success
  message, exits, and frees the address space. A truncated ELF image is also
  rejected. This path was built and observed passing in QEMU serial output.
- The desktop uses the Aurora visual theme and desktop icons now launch their
  matching applications. The GUI ISO was built and the Terminal icon launch
  was verified in QEMU.

## Project tools and what they do

Use the Linux toolchain inside WSL on Windows; the repository is available at
`/home/lui/MyOS` from Ubuntu WSL.

| Tool | Purpose |
| --- | --- |
| GNU Make (`make`) | Runs the ordered build rules and produces the kernel ISO. |
| GCC (`gcc`) | Compiles freestanding x86-64 C and assembly (`-m64`); no host libc is linked into the kernel. |
| GNU `ld` | Links the kernel and the standalone user ELF64 test image with their respective linker scripts. |
| GNU `objcopy` | Embeds the built user ELF test image in the kernel as binary data; it is also used for app packaging. |
| GRUB tools (`grub-mkrescue`) | Assemble the bootable ISO from the kernel and GRUB configuration. |
| QEMU (`qemu-system-x86_64`) | Boots the ISO for runtime testing; serial output is useful for headless checks. |
| Python 3 | Supports SOLFS disk-image and SPX package generation in the build. |
| Git (`git diff`, `git status`) | Reviews the changes and detects whitespace or worktree issues. Do not reset or revert unrelated dirty work. |

Useful commands:

```sh
cd /home/lui/MyOS
make -j2 build/solis.iso
make run
make run-serial
git diff --check
git status --short
```

The Makefile's `get` target installs supported build/run dependencies on
Debian/Ubuntu, Fedora, Arch, or openSUSE. Avoid installing packages unless
the required tool is missing.

## Next milestones

Prioritize these in order; build and boot-test each coherent milestone before
adding another:

1. **Harden ELF loading.** Add tests for malformed program-header tables,
   overflowing segment ranges, overlapping load pages, and unsupported
   dynamic/interpreter segments. Consider a host-side parser test or a
   boot-time self-test that checks rejection without leaking pages.
2. **Introduce a real process/task abstraction.** Track an address space,
   user instruction/stack state, kernel stack, and exit status per task. Ensure
   interrupts and syscalls always use the correct kernel stack and CR3.
3. **Add a small scheduler.** Start with a controlled round-robin switch
   between kernel tasks, then schedule isolated ring-3 tasks. Do not treat the
   current one-shot `arch_enter_user` boot test as a process scheduler.
4. **Grow the syscall ABI deliberately.** Add bounded read/write and task
   management only when task lifetime and user-buffer validation are defined.
   Keep the ABI documented and test invalid pointers, lengths, and syscall
   numbers.
5. **Move a minimal application to user mode.** Start with a small command-line
   test program and a user-space runtime; only then consider porting GUI apps.
   Current SPX app callbacks rely on kernel services and are not isolated.
6. **Expand hardware and boot coverage.** Test the ISO under BIOS and UEFI,
   validate CPU feature fallbacks (especially NX), and add SMP/APIC support
   only after per-CPU state and synchronization primitives are ready.

## Handoff cautions

- The worktree already contains many changes from earlier work. Inspect
  `git status --short` before editing, and never discard unrelated modifications.
- The attached `file_tss.c` was supplied as a read-only snapshot; it is not a
  project file to edit.
- Do not publish or push changes to the experimental repository unless the
  user's message explicitly includes `!experimental`.
- The self-test is intentionally early in kernel startup, before graphics and
  device initialization, so its result appears in serial output.
- This loader is for the kernel-embedded static test image only. It is not yet
  a filesystem executable loader or a safe general-purpose process loader.

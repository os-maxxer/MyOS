#!/usr/bin/env bash
# Build a self-booting UEFI VDI for Solis.
#
# The disk layout is:
#   1. 64 MiB FAT32 EFI System Partition (GRUB + kernel)
#   2. 4 MiB SolFS partition, preloaded with the bundled packages
#
# UEFI firmware boots /EFI/BOOT/BOOTX64.EFI. BIOS boot is intentionally not
# installed here; use the ISO plus a data VDI for BIOS-only machines.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
build="${BUILD_DIR:-$root/build}"
kernel="$build/solis.kernel"
user_test="$build/user_test.elf"
mksolfs="$root/tools/mksolfs.py"
grub_cfg="$root/boot/grub/grub-uefi.cfg"
out_img="$build/solis-uefi.img"
out_vdi="$build/solis-uefi.vdi"

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

for f in "$kernel" "$user_test" "$mksolfs" "$grub_cfg"; do
    [[ -s "$f" ]] || fail "missing $f; run make first"
done

for tool in sgdisk mformat mmd mcopy grub-mkstandalone python3 dd truncate stat sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool not found: $tool"
done

if command -v qemu-img >/dev/null 2>&1; then
    convert_with_vbox=0
elif command -v VBoxManage >/dev/null 2>&1; then
    convert_with_vbox=1
else
    fail "install qemu-img or VBoxManage to create the VDI"
fi

mkdir -p "$build"
work="$(mktemp -d "$build/uefi-work.XXXXXX")"
trap 'rm -rf "$work"' EXIT

esp="$work/esp.img"
raw="$work/disk.img"
solfs="$work/solfs.img"
efi="$work/BOOTX64.EFI"

# A 64 MiB FAT32 ESP. -T sets the total sector count for the volume.
truncate -s 64M "$esp"
mformat -i "$esp" -F -T 131072 ::
mmd -i "$esp" ::/EFI ::/EFI/BOOT ::/boot ::/boot/grub

grub-mkstandalone -O x86_64-efi -o "$efi" \
    --modules="part_gpt fat normal multiboot2 search search_fs_file all_video gfxterm" \
    --locales= --fonts=unicode --themes= \
    "boot/grub/grub.cfg=$grub_cfg"

mcopy -i "$esp" "$efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$esp" "$grub_cfg" ::/boot/grub/grub.cfg
mcopy -i "$esp" "$kernel" ::/boot/solis.kernel

python3 "$mksolfs" "$root/pkg/" "$solfs" "$user_test" >/dev/null
[[ "$(stat -c %s "$solfs")" -eq 4194304 ]] \
    || fail "unexpected SolFS image size; expected 4 MiB"

# GPT: ESP starts at LBA 2048, SolFS starts immediately after the 64 MiB ESP.
truncate -s 128M "$raw"
sgdisk --clear --set-alignment=2048 \
    --new=1:2048:+64M --typecode=1:EF00 --change-name=1:"Solis ESP" \
    --new=2:133120:+4M --typecode=2:7A5D9E13-45B4-4C2A-8C74-534F4C495346 --change-name=2:"Solis" \
    "$raw" >/dev/null
dd if="$esp" of="$raw" bs=512 seek=2048 conv=notrunc status=none
dd if="$solfs" of="$raw" bs=512 seek=133120 conv=notrunc status=none

mv "$raw" "$out_img"
rm -f "$out_vdi"
if ((convert_with_vbox)); then
    VBoxManage convertfromraw "$out_img" "$out_vdi" --format VDI >/dev/null
else
    qemu-img convert -f raw -O vdi "$out_img" "$out_vdi"
fi

printf 'Created %s\n' "$out_vdi"
printf 'Created %s\n' "$out_img"
sha256sum "$out_img" "$out_vdi"

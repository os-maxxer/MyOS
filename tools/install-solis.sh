#!/usr/bin/env bash
set -euo pipefail

ESP_GUID="c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
BIOS_GUID="21686148-6449-6e6f-744e-656564454649"
SOLIS_GUID="7a5d9e13-45b4-4c2a-8c74-534f4c495346"
SOLFS_SECTORS=8192
SOLFS_BYTES=$((SOLFS_SECTORS * 512))

usage() {
    cat <<'EOF'
Usage:
  tools/install-solis.sh --disk DISK --esp PARTITION \
    --bios-boot PARTITION --solfs PARTITION [--apply]

Prepare a GPT disk yourself with these existing partitions:
  ESP:       FAT32 EFI System Partition, at least 64 MiB
  BIOS boot: unformatted BIOS boot partition, at least 1 MiB
  SOLFS:     at least 4 MiB, GPT type GUID 7A5D9E13-45B4-4C2A-8C74-534F4C495346

Without --apply, the script only validates and prints the selected targets.
With --apply, it installs BIOS and UEFI GRUB, adds a UEFI boot entry, and
formats only the selected SOLFS partition. It does not create or resize
partitions. Review the warnings and type the requested confirmation.
EOF
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

DISK=""
ESP=""
BIOS_BOOT=""
SOLFS=""
APPLY=0

while (($#)); do
    case "$1" in
        --disk) (($# >= 2)) || fail "--disk requires a device"; DISK=$2; shift 2 ;;
        --esp) (($# >= 2)) || fail "--esp requires a partition"; ESP=$2; shift 2 ;;
        --bios-boot) (($# >= 2)) || fail "--bios-boot requires a partition"; BIOS_BOOT=$2; shift 2 ;;
        --solfs) (($# >= 2)) || fail "--solfs requires a partition"; SOLFS=$2; shift 2 ;;
        --apply) APPLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; fail "unknown argument: $1" ;;
    esac
done

[[ -n "$DISK" && -n "$ESP" && -n "$BIOS_BOOT" && -n "$SOLFS" ]] || {
    usage >&2
    fail "all disk and partition arguments are required"
}

for tool in readlink lsblk sgdisk blockdev findmnt blkid awk grub-install efibootmgr \
            python3 stat mktemp mount umount sync dd cp mkdir xargs; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool not found: $tool"
done

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
KERNEL="$REPO_ROOT/build/solis.kernel"
GRUB_CONFIG="$REPO_ROOT/boot/grub/grub.cfg"
MKSOLFS="$REPO_ROOT/tools/mksolfs.py"
[[ -s "$KERNEL" ]] || fail "build/solis.kernel is missing; run make first"
[[ -f "$GRUB_CONFIG" && -f "$MKSOLFS" ]] || fail "installer files are missing"

DISK="$(readlink -f -- "$DISK")"
ESP="$(readlink -f -- "$ESP")"
BIOS_BOOT="$(readlink -f -- "$BIOS_BOOT")"
SOLFS="$(readlink -f -- "$SOLFS")"

[[ -b "$DISK" ]] || fail "disk is not a block device: $DISK"
disk_type="$(lsblk -dn -o TYPE "$DISK" | xargs)"
[[ "$disk_type" == "disk" || "$disk_type" == "loop" ]] || fail "target is not a whole disk or loop-backed disk image: $DISK"
[[ "$(blockdev --getss "$DISK")" == "512" ]] || fail "the selected disk must use 512-byte logical sectors"
mounted_on_disk="$(lsblk -nrpo MOUNTPOINT "$DISK" | awk 'NF { print; exit }')"
[[ -z "$mounted_on_disk" ]] || fail "unmount all filesystems on $DISK before installing"

disk_name="$(basename -- "$DISK")"
partition_number() {
    local partition="$1"
    [[ -b "$partition" ]] || fail "partition is not a block device: $partition"
    [[ "$(lsblk -dn -o TYPE "$partition" | xargs)" == "part" ]] || fail "not a partition: $partition"
    [[ "$(lsblk -dn -o PKNAME "$partition" | xargs)" == "$disk_name" ]] || fail "partition is not on $DISK: $partition"
    local mounted
    mounted="$(findmnt -rn -S "$partition" -o TARGET || true)"
    [[ -z "$mounted" ]] || fail "unmount $partition before installing (mounted at $mounted)"
    lsblk -dn -o PARTN "$partition" | xargs
}

esp_number="$(partition_number "$ESP")"
bios_number="$(partition_number "$BIOS_BOOT")"
solfs_number="$(partition_number "$SOLFS")"
[[ "$esp_number" != "$bios_number" && "$esp_number" != "$solfs_number" && "$bios_number" != "$solfs_number" ]] || fail "ESP, BIOS boot, and SOLFS partitions must be different"

partition_guid() {
    sgdisk -i "$1" "$DISK" | awk -F': ' '/Partition GUID code/ { sub(/ .*/, "", $2); print tolower($2); exit }'
}

[[ "$(partition_guid "$esp_number")" == "$ESP_GUID" ]] || fail "$ESP must have the GPT EFI System Partition type"
[[ "$(partition_guid "$bios_number")" == "$BIOS_GUID" ]] || fail "$BIOS_BOOT must have the GPT BIOS boot type"
[[ "$(partition_guid "$solfs_number")" == "$SOLIS_GUID" ]] || fail "$SOLFS must have the Solis data type GUID"
[[ "$(blkid -s TYPE -o value "$ESP")" == "vfat" ]] || fail "ESP must be formatted as FAT"
[[ "$(blockdev --getsize64 "$ESP")" -ge $((64 * 1024 * 1024)) ]] || fail "ESP must be at least 64 MiB"
[[ "$(blockdev --getsize64 "$BIOS_BOOT")" -ge $((1024 * 1024)) ]] || fail "BIOS boot partition must be at least 1 MiB"
[[ "$(blockdev --getsize64 "$SOLFS")" -ge "$SOLFS_BYTES" ]] || fail "SOLFS partition must be at least 4 MiB"

[[ -d /usr/lib/grub/i386-pc && -d /usr/lib/grub/x86_64-efi ]] || fail "install both BIOS and EFI GRUB platform modules"
if ((APPLY)); then
    [[ "$EUID" -eq 0 ]] || fail "run with root privileges to install"
    [[ -t 0 ]] || fail "interactive confirmation is required"
    [[ -d /sys/firmware/efi/efivars ]] || fail "run this installer in UEFI mode to register the UEFI boot entry"
fi

printf 'Disk:       %s\nESP:        %s\nBIOS boot:  %s\nSolis data: %s\n' \
    "$DISK" "$ESP" "$BIOS_BOOT" "$SOLFS"
printf 'No partitions will be created or resized.\n'
printf 'The selected SOLFS partition will be overwritten (first 4 MiB).\n'
printf 'GRUB will use the ESP /boot directory, reserve the BIOS boot partition,\n'
printf 'replace the disk boot code, and add a UEFI boot entry.\n'

if ((!APPLY)); then
    printf 'Validation only; no disk was modified. Pass --apply to install.\n'
    exit 0
fi

WORK_DIR="$(mktemp -d)"
ESP_MOUNT="$WORK_DIR/esp"
mkdir "$ESP_MOUNT"
mounted=0
cleanup() {
    if ((mounted)); then
        umount "$ESP_MOUNT"
    fi
    rm -rf -- "$WORK_DIR"
}
trap cleanup EXIT

python3 "$MKSOLFS" "$REPO_ROOT/pkg/" "$WORK_DIR/solfs.img"
[[ "$(stat -c %s "$WORK_DIR/solfs.img")" == "$SOLFS_BYTES" ]] || fail "unexpected SOLFS image size"

printf 'Type INSTALL SOLIS ON %s to continue: ' "$disk_name"
IFS= read -r confirmation
[[ "$confirmation" == "INSTALL SOLIS ON $disk_name" ]] || fail "confirmation did not match; no disk was modified"

mount "$ESP" "$ESP_MOUNT"
mounted=1
[[ ! -e "$ESP_MOUNT/boot" && ! -e "$ESP_MOUNT/EFI/Solis" ]] \
    || fail "use an ESP with no existing /boot or EFI/Solis directory"
mkdir -p "$ESP_MOUNT/boot"
grub-install --target=i386-pc --boot-directory="$ESP_MOUNT/boot" --recheck "$DISK"
grub-install --target=x86_64-efi --efi-directory="$ESP_MOUNT" \
    --boot-directory="$ESP_MOUNT/boot" --bootloader-id=Solis --no-nvram --recheck
cp "$KERNEL" "$ESP_MOUNT/boot/solis.kernel"
cp "$GRUB_CONFIG" "$ESP_MOUNT/boot/grub/grub.cfg"
sync
efibootmgr --create --disk "$DISK" --part "$esp_number" \
    --label "Solis" --loader '\EFI\Solis\grubx64.efi'
dd if="$WORK_DIR/solfs.img" of="$SOLFS" bs=512 count="$SOLFS_SECTORS" conv=fsync status=progress
sync

printf 'Solis installation completed. Keep the ISO until the installed system boots successfully.\n'

#!/usr/bin/env bash
# Create build/solis-blank.vdi for VirtualBox.
#
# Prefer VirtualBox's own createmedium: it produces a native VDI and registers
# it, avoiding the stale-UUID warning caused by replacing a registered medium
# with a newly generated qemu-img file.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
build="${BUILD_DIR:-$root/build}"
out="$build/solis-blank.vdi"
mkdir -p "$build"

vbox=""
if command -v VBoxManage >/dev/null 2>&1; then
    vbox="$(command -v VBoxManage)"
elif [ -x "/c/Program Files/Oracle/VirtualBox/VBoxManage.exe" ]; then
    vbox="/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"
elif [ -x "/mnt/c/Program Files/Oracle/VirtualBox/VBoxManage.exe" ]; then
    vbox="/mnt/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"
fi

if [ -n "$vbox" ]; then
    if command -v cygpath >/dev/null 2>&1; then
        win_out="$(cygpath -w "$out")"
    elif command -v wslpath >/dev/null 2>&1; then
        win_out="$(wslpath -w "$out")"
    else
        echo "Error: cannot convert $out to a Windows path" >&2
        exit 1
    fi

    if [ -f "$out" ]; then
        if "$vbox" showmediuminfo disk "$win_out" 2>/dev/null | grep -q '^In use by VMs:'; then
            echo "Error: $out is in use; detach it in VirtualBox first" >&2
            exit 1
        fi
        old_uuid="$("$vbox" list hdds 2>/dev/null | VBX_VDI="$win_out" awk '
            BEGIN { loc = ENVIRON["VBX_VDI"] }
            /^UUID:/ { uuid = $2 }
            /^Location:/ {
                path = $0
                sub(/^Location:[ \t]*/, "", path)
                sub(/\r$/, "", path)
                if (path == loc) { print uuid; exit }
            }
        ' | tr -d '\r')"
        if [ -n "$old_uuid" ]; then
            "$vbox" closemedium disk "$old_uuid" >/dev/null || true
        fi
        rm -f "$out"
    fi

    "$vbox" createmedium disk --filename "$win_out" --size 8 --format VDI
    exit 0
fi

if command -v qemu-img >/dev/null 2>&1; then
    rm -f "$out"
    qemu-img create -f vdi "$out" 8M
    exit 0
fi

echo "Error: install VirtualBox or qemu-img to create $out" >&2
exit 1
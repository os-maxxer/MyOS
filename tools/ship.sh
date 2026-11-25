#!/usr/bin/env bash
# Build Solis in WSL and atomically copy the launch images into D:\MyOS\build.
# Run this from Git Bash on Windows:  bash tools/ship.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="$root/build"
stamp="$(date -u +%Y%m%d-%H%M%S)"
mkdir -p "$dest"

# Keep the VirtualBox medium UUID stable across rebuilds. VirtualBox stores the
# UUID in its media registry and in the VM attachment; replacing the VDI with a
# freshly generated one makes the registered disk inaccessible until the UUIDs
# match again.
vbox=""
if command -v VBoxManage >/dev/null 2>&1; then
  vbox="$(command -v VBoxManage)"
elif [ -x "/c/Program Files/Oracle/VirtualBox/VBoxManage.exe" ]; then
  vbox="/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"
fi

old_uefi_uuid=""
if [ -n "$vbox" ]; then
  if "$vbox" list runningvms 2>/dev/null | grep -q '"solis"'; then
    echo "[ship] VirtualBox VM 'solis' is running; power it off before shipping" >&2
    exit 1
  fi
  if [ -f "$dest/solis-uefi.vdi" ] && command -v cygpath >/dev/null 2>&1; then
    win_uefi="$(cygpath -w "$dest/solis-uefi.vdi")"
    old_uefi_uuid="$("$vbox" list hdds 2>/dev/null | VBX_VDI="$win_uefi" awk '
      BEGIN { loc = ENVIRON["VBX_VDI"] }
      /^UUID:/ { uuid = $2 }
      /^Location:/ {
        path = $0
        sub(/^Location:[ \t]*/, "", path)
        sub(/\r$/, "", path)
        if (path == loc) { print uuid; exit }
      }
    ' | tr -d '\r')"
  fi
fi

echo "[ship] syncing source to WSL"
(
  cd "$root"
  tar -cf - --exclude=.git --exclude=build --exclude=screenshots \
      --exclude=qemu.log . | \
  wsl --cd /tmp -e bash -c '
    dest="$HOME/solis-tick"
    rm -rf "$dest"
    mkdir -p "$dest"
    tar -xf - -C "$dest"
  '
)

# The source copy has no .git, so read the commit here and hand it to make:
# without it the build id says "nogit" and points at nothing.
git_sha="$(cd "$root" && git rev-parse --short HEAD 2>/dev/null || echo nogit)"

echo "[ship] building kernel, ISO, package disk, and UEFI VDI (commit $git_sha)"
wsl --cd /tmp -e bash -c 'cd "$HOME/solis-tick" && make -j"$(nproc)" SOLIS_GIT_SHA='"$git_sha"' build/solis.iso build/disk.img build/solis-uefi.vdi'

# Refuse to ship images that do not actually contain this build. This is the
# check that would have caught the VDI carrying a 25 KB ck3.spx.
echo "[ship] running host tests"
wsl --cd /tmp -e bash -c 'cd "$HOME/solis-tick" && make test-ck3'

echo "[ship] verifying artifact contents"
wsl --cd /tmp -e bash -c 'cd "$HOME/solis-tick" && python3 tools/verify_images.py --build-dir build'

echo "[ship] copying images back"
staging="$dest/.ship-staging"
rm -rf "$staging"
mkdir -p "$staging"
wsl --cd /tmp -e bash -c 'cd "$HOME/solis-tick/build" && tar -cf - solis.iso disk.img solis-uefi.vdi' | \
  tar -xf - -C "$staging"

local_iso="$(sha256sum "$staging/solis.iso" | awk '{print $1}')"
local_disk="$(sha256sum "$staging/disk.img" | awk '{print $1}')"
local_uefi="$(sha256sum "$staging/solis-uefi.vdi" | awk '{print $1}')"
remote_iso="$(wsl --cd /tmp -e bash -c 'sha256sum "$HOME/solis-tick/build/solis.iso"' | awk '{print $1}')"
remote_disk="$(wsl --cd /tmp -e bash -c 'sha256sum "$HOME/solis-tick/build/disk.img"' | awk '{print $1}')"
remote_uefi="$(wsl --cd /tmp -e bash -c 'sha256sum "$HOME/solis-tick/build/solis-uefi.vdi"' | awk '{print $1}')"

if [ "$local_iso" != "$remote_iso" ] || [ "$local_disk" != "$remote_disk" ] || \
   [ "$local_uefi" != "$remote_uefi" ]; then
  echo "[ship] hash mismatch; local build was not replaced" >&2
  rm -rf "$staging"
  exit 1
fi

# Restore the registered VirtualBox UUID on the new image before it replaces
# the old file. This keeps an existing VM attachment valid.
if [ -n "$vbox" ] && [ -n "$old_uefi_uuid" ]; then
  if ! command -v cygpath >/dev/null 2>&1; then
    echo "[ship] cygpath not found; cannot preserve the VirtualBox VDI UUID" >&2
    rm -rf "$staging"
    exit 1
  fi
  win_staging_vdi="$(cygpath -w "$staging/solis-uefi.vdi")"
  if ! "$vbox" internalcommands sethduuid "$win_staging_vdi" "$old_uefi_uuid" >/dev/null; then
    echo "[ship] could not restore the VirtualBox VDI UUID; old disk was kept" >&2
    rm -rf "$staging"
    exit 1
  fi
  local_uefi="$(sha256sum "$staging/solis-uefi.vdi" | awk '{print $1}')"
fi

mv "$staging/solis.iso" "$dest/solis.iso.tmp"
mv "$staging/disk.img" "$dest/disk.img.tmp"
mv "$staging/solis-uefi.vdi" "$dest/solis-uefi.vdi.tmp"
mv "$dest/solis.iso.tmp" "$dest/solis.iso"
mv "$dest/disk.img.tmp" "$dest/disk.img"
mv "$dest/solis-uefi.vdi.tmp" "$dest/solis-uefi.vdi"
rm -rf "$staging"

cat > "$dest/BUILD_ID" <<EOF
build=$stamp
iso=$local_iso
disk=$local_disk
uefi=$local_uefi
EOF

echo "[ship] local and WSL hashes match"
echo "[ship] solis.iso        $local_iso"
echo "[ship] disk.img         $local_disk"
echo "[ship] solis-uefi.vdi  $local_uefi"
echo "[ship] build      $stamp"
echo "[ship] wrote $dest/BUILD_ID"

#!/usr/bin/env python3
"""Create a SOLFS disk image with pre-loaded package files.

Incremental: if the output image already exists and is a valid SOLFS disk,
entries that are not package files (user files and directories created by the
OS) are preserved. Package files (repo.json, *.spx, *.html and image assets)
are always refreshed from the package directory.
"""
import struct
import sys
import os

SECTOR_SIZE = 512
DISK_SECTORS = 8192  # 4MB

SOLFS_MAGIC = 0x53464F53
SOLFS_VERSION = 2
SOLFS_FAT_ENTRIES = 8192
SOLFS_FAT_SECTORS = 32
SOLFS_DIR_ENTRIES = 64
SOLFS_DIR_SECTORS = 4
SOLFS_DATA_START = 37

SOLFS_FAT_FREE = 0x0000
SOLFS_FAT_EOF = 0xFFFF
SOLFS_ATTR_DIR = 0x01
SOLFS_MAX_FILE_SIZE = (DISK_SECTORS - SOLFS_DATA_START) * SECTOR_SIZE

# The account record written by kernel/setup.c. A development image can carry
# one so every rebuild boots straight to sign-in instead of replaying the
# first-boot wizard. Defaults mirror setup.c exactly; a wrong value here shows
# up as the wizard appearing again, not as a corrupt disk.
SETUP_PATH = 'settings/account'
SETUP_MAGIC = 0x53455450
SETUP_VERSION = 2
SETUP_EDITION_EXTRA = 2
GUI_THEME_SOLIS_UI_ENGINE = 8
MASK64 = (1 << 64) - 1


def setup_hash_password(password, salt):
    """The salted hash from kernel/setup.c, reproduced byte for byte."""
    value = 1469598103934665603
    value ^= (salt * 0x9E3779B97F4A7C15) & MASK64
    value &= MASK64
    for ch in password.encode('utf-8'):
        value ^= ch
        value = (value * 1099511628211) & MASK64
    value ^= value >> 33
    value = (value * 0xFF51AFD7ED558CCD) & MASK64
    value ^= value >> 33
    return value & MASK64


def dev_account_entry(spec):
    """Turn "user:password" into (filename, record, attrs), or None."""
    if not spec or ':' not in spec:
        return None
    username, _, password = spec.partition(':')
    username = username.strip()[:23]
    if not username or not password:
        return None
    salt = 0x51A7C0DE
    record = struct.pack(
        '<IHBBB3sIQ24sBB2s',
        SETUP_MAGIC,
        SETUP_VERSION,
        1,                      # installed
        GUI_THEME_SOLIS_UI_ENGINE,
        0,                      # timezone index 0 == UTC
        b'\x00\x00\x00',
        salt,
        setup_hash_password(password, salt),
        username.encode('utf-8'),
        SETUP_EDITION_EXTRA,
        0,                      # not a guest account
        b'\x00\x00',
    )
    return (SETUP_PATH, record, 0)


def load_existing_disk(path):
    """Return {name: (data, attrs)} for a valid existing SOLFS image, or {}."""
    if not os.path.isfile(path):
        return {}
    with open(path, 'rb') as f:
        disk = f.read()
    if len(disk) < DISK_SECTORS * SECTOR_SIZE:
        return {}

    magic = struct.unpack_from('<I', disk, 0)[0]
    version = struct.unpack_from('<I', disk, 4)[0]
    if magic != SOLFS_MAGIC or version != SOLFS_VERSION:
        return {}

    fat = [struct.unpack_from('<H', disk, (1 + i) * SECTOR_SIZE + j * 2)[0]
           for i in range(SOLFS_FAT_SECTORS)
           for j in range(SECTOR_SIZE // 2)]

    existing = {}
    for i in range(SOLFS_DIR_ENTRIES):
        off = (SOLFS_DATA_START - SOLFS_DIR_SECTORS) * SECTOR_SIZE + i * 32
        e = disk[off:off + 32]
        name = e[0:24].split(b'\x00')[0].decode('latin1')
        if not name:
            continue
        attrs = e[24]
        cluster = struct.unpack_from('<H', e, 25)[0]
        size = struct.unpack_from('<I', e, 27)[0]

        if attrs & SOLFS_ATTR_DIR:
            existing[name] = (b'', attrs)
            continue

        data = bytearray()
        cl = cluster
        while cl >= 2 and cl < SOLFS_FAT_EOF:
            lba = SOLFS_DATA_START + (cl - 2)
            data += disk[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE]
            cl = fat[cl]
            if len(data) > SOLFS_MAX_FILE_SIZE:
                break
        existing[name] = (bytes(data[:size]), attrs)

    return existing


def collect_packages(pkg_dir):
    """Return {name: data} of package files in pkg_dir (recursive, flattened)."""
    packages = {}
    for root, dirs, fnames in os.walk(pkg_dir):
        for fname in sorted(fnames):
            fpath = os.path.join(root, fname)
            if not os.path.isfile(fpath):
                continue
            if (fname.endswith('.spx') or fname.endswith('.html') or
                    fname.endswith('.htm') or fname.endswith('.png') or
                    fname.endswith('.bmp') or fname.endswith('.jpg') or
                    fname.endswith('.jpeg') or fname.endswith('.gif') or
                    fname.endswith('.ico') or fname.endswith('.tga') or
                    fname.endswith('.ppm') or fname.endswith('.pgm') or
                    fname.endswith('.pbm') or fname.endswith('.qoi') or
                    fname.endswith('.xbm') or fname == 'repo.json'):
                with open(fpath, 'rb') as f:
                    packages[fname] = f.read()
    return packages


def create_disk(pkg_dir, output_path, user_elf=None):
    # Packages always take precedence over any same-named existing entry.
    files = [(name, data, 0) for name, data in collect_packages(pkg_dir).items()]

    # Optional development account: MKSOLFS_DEV_ACCOUNT=user:password boots
    # straight to sign-in. Opt-in, never the default, and it only ever writes
    # the normal account record, so the login screen still checks the password.
    dev_spec = os.environ.get('MKSOLFS_DEV_ACCOUNT', '').strip()
    if dev_spec:
        entry = dev_account_entry(dev_spec)
        if entry is None:
            print("Error: MKSOLFS_DEV_ACCOUNT must be user:password",
                  file=sys.stderr)
            sys.exit(1)
        files = [f for f in files if f[0] != entry[0]]
        files.append(entry)
        print("Note: development account written; the setup wizard is skipped")

    if user_elf:
        if not os.path.isfile(user_elf):
            print(f"Error: user ELF does not exist: {user_elf}")
            sys.exit(1)
        with open(user_elf, 'rb') as f:
            elf_data = f.read()
        if not elf_data:
            print(f"Error: user ELF is empty: {user_elf}")
            sys.exit(1)
        if len(elf_data) > SOLFS_MAX_FILE_SIZE:
            print(f"Error: user ELF exceeds SOLFS size limit: {user_elf}")
            sys.exit(1)
        files = [item for item in files if item[0] != 'hello.elf']
        files.append(('hello.elf', elf_data, 0))

    # Preserve user files and directories from an existing image.
    for name, (data, attrs) in load_existing_disk(output_path).items():
        if name not in [f[0] for f in files]:
            files.append((name, data, attrs))

    # Calculate needed clusters (directories use no clusters).
    total_clusters_needed = sum(
        (len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE for _, data, attrs in files
        if not (attrs & SOLFS_ATTR_DIR))

    if total_clusters_needed > SOLFS_FAT_ENTRIES - 2:
        print(f"Error: need {total_clusters_needed} clusters, max {SOLFS_FAT_ENTRIES - 2}")
        sys.exit(1)

    if len(files) > SOLFS_DIR_ENTRIES:
        print(f"Error: {len(files)} files, max {SOLFS_DIR_ENTRIES}")
        sys.exit(1)

    # Create disk image
    disk = bytearray(DISK_SECTORS * SECTOR_SIZE)

    # --- Superblock at LBA 0 ---
    sb = struct.pack('<II', SOLFS_MAGIC, SOLFS_VERSION)
    sb += struct.pack('<H', SECTOR_SIZE)        # block_size
    sb += struct.pack('<I', DISK_SECTORS)        # total_sectors
    sb += struct.pack('<I', 1)                   # fat_start
    sb += struct.pack('<I', SOLFS_FAT_SECTORS)    # fat_sectors
    sb += struct.pack('<I', SOLFS_DATA_START - SOLFS_DIR_SECTORS)  # root_dir_start
    sb += struct.pack('<I', SOLFS_DIR_ENTRIES)    # root_dir_entries
    sb += struct.pack('<I', SOLFS_DATA_START)     # data_start
    sb += struct.pack('<Q', 0x494C4F53)          # machine_id "SOLI"
    sb += b'\x00' * (SECTOR_SIZE - len(sb))     # padding
    disk[0:SECTOR_SIZE] = sb[:SECTOR_SIZE]

    # --- FAT at LBA 1..32 ---
    fat = bytearray(SOLFS_FAT_SECTORS * SECTOR_SIZE)
    # Mark entries 0 and 1 as used
    struct.pack_into('<H', fat, 0, 0x0001)
    struct.pack_into('<H', fat, 2, 0x0001)

    next_cluster = 2
    file_entries = []

    for fname, data, attrs in files:
        if attrs & SOLFS_ATTR_DIR:
            file_entries.append((fname, 0, 0, attrs))
            continue

        first_cluster = next_cluster if data else 0
        file_size = len(data)
        clusters_needed = (file_size + SECTOR_SIZE - 1) // SECTOR_SIZE

        # Write data sectors
        offset = 0
        for i in range(clusters_needed):
            cl = next_cluster
            lba = SOLFS_DATA_START + (cl - 2)
            chunk = data[offset:offset + SECTOR_SIZE]
            chunk = chunk.ljust(SECTOR_SIZE, b'\x00')
            disk[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE] = chunk[:SECTOR_SIZE]
            offset += SECTOR_SIZE

            if i < clusters_needed - 1:
                struct.pack_into('<H', fat, cl * 2, cl + 1)
            else:
                struct.pack_into('<H', fat, cl * 2, SOLFS_FAT_EOF)

            next_cluster += 1

        file_entries.append((fname, first_cluster, file_size, attrs))

    # Write FAT to disk
    for i in range(SOLFS_FAT_SECTORS):
        lba = 1 + i
        disk[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE] = fat[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE]

    # --- Root directory at LBA 33..34 ---
    dir_area = bytearray(SOLFS_DIR_SECTORS * SECTOR_SIZE)
    for idx, (fname, first_cluster, file_size, attrs) in enumerate(file_entries):
        if idx >= SOLFS_DIR_ENTRIES:
            break
        entry = struct.pack('<24s', fname.encode()[:24])
        entry += struct.pack('<B', attrs)
        entry += struct.pack('<H', first_cluster)
        entry += struct.pack('<I', file_size)
        entry += b'\x00'                      # padding
        offset = idx * 32
        dir_area[offset:offset + len(entry)] = entry[:32]

    for i in range(SOLFS_DIR_SECTORS):
        lba = SOLFS_DATA_START - SOLFS_DIR_SECTORS + i
        disk[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE] = dir_area[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE]

    # Write output
    with open(output_path, 'wb') as f:
        f.write(disk)

    print(f"Created {output_path} ({len(files)} files, {next_cluster - 2} clusters used)")
    for fname, _, size, attrs in file_entries:
        tag = "dir " if attrs & SOLFS_ATTR_DIR else ""
        print(f"  {tag}{fname}: {size} bytes")


if __name__ == '__main__':
    if len(sys.argv) not in (3, 4):
        print(f"Usage: {sys.argv[0]} <pkg_directory> <output_image> [user_elf]")
        sys.exit(1)
    create_disk(sys.argv[1], sys.argv[2],
                sys.argv[3] if len(sys.argv) == 4 else None)

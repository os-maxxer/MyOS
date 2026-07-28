#!/usr/bin/env python3
"""Create a NOFS disk image with pre-loaded package files."""
import struct
import sys
import os

SECTOR_SIZE = 512
DISK_SECTORS = 8192  # 4MB

NOFS_MAGIC = 0x53464F4E
NOFS_VERSION = 1
NOFS_FAT_ENTRIES = 8192
NOFS_FAT_SECTORS = 32
NOFS_DIR_ENTRIES = 32
NOFS_DIR_SECTORS = 2
NOFS_DATA_START = 35

def create_disk(pkg_dir, output_path):
    # Read all files from pkg directory (recursive, flatten names)
    files = []
    for root, dirs, fnames in os.walk(pkg_dir):
        for fname in sorted(fnames):
            fpath = os.path.join(root, fname)
            if os.path.isfile(fpath):
                # Use basename only (flatten)
                if fname.endswith('.npx') or fname == 'repo.json':
                    with open(fpath, 'rb') as f:
                        data = f.read()
                    files.append((fname, data))

    # Calculate needed clusters
    total_clusters_needed = sum((len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE for _, data in files)

    if total_clusters_needed > NOFS_FAT_ENTRIES - 2:
        print(f"Error: need {total_clusters_needed} clusters, max {NOFS_FAT_ENTRIES - 2}")
        sys.exit(1)

    if len(files) > NOFS_DIR_ENTRIES:
        print(f"Error: {len(files)} files, max {NOFS_DIR_ENTRIES}")
        sys.exit(1)

    # Create disk image
    disk = bytearray(DISK_SECTORS * SECTOR_SIZE)

    # --- Superblock at LBA 0 ---
    sb = struct.pack('<II', NOFS_MAGIC, NOFS_VERSION)
    sb += struct.pack('<H', SECTOR_SIZE)        # block_size
    sb += struct.pack('<I', DISK_SECTORS)        # total_sectors
    sb += struct.pack('<I', 1)                   # fat_start
    sb += struct.pack('<I', NOFS_FAT_SECTORS)    # fat_sectors
    sb += struct.pack('<I', NOFS_DATA_START - NOFS_DIR_SECTORS)  # root_dir_start
    sb += struct.pack('<I', NOFS_DIR_ENTRIES)    # root_dir_entries
    sb += struct.pack('<I', NOFS_DATA_START)     # data_start
    sb += struct.pack('<Q', 0x4E59584F)          # machine_id "NYXO"
    sb += b'\x00' * (SECTOR_SIZE - len(sb))     # padding
    disk[0:SECTOR_SIZE] = sb[:SECTOR_SIZE]

    # --- FAT at LBA 1..32 ---
    fat = bytearray(NOFS_FAT_SECTORS * SECTOR_SIZE)
    # Mark entries 0 and 1 as used
    struct.pack_into('<H', fat, 0, 0x0001)
    struct.pack_into('<H', fat, 2, 0x0001)

    next_cluster = 2
    file_entries = []

    for fname, data in files:
        first_cluster = next_cluster
        file_size = len(data)
        clusters_needed = (file_size + SECTOR_SIZE - 1) // SECTOR_SIZE

        # Write data sectors
        offset = 0
        for i in range(clusters_needed):
            cl = next_cluster
            lba = NOFS_DATA_START + (cl - 2)
            chunk = data[offset:offset + SECTOR_SIZE]
            chunk = chunk.ljust(SECTOR_SIZE, b'\x00')
            disk[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE] = chunk[:SECTOR_SIZE]
            offset += SECTOR_SIZE

            if i < clusters_needed - 1:
                struct.pack_into('<H', fat, cl * 2, cl + 1)
            else:
                struct.pack_into('<H', fat, cl * 2, 0xFFFF)

            next_cluster += 1

        file_entries.append((fname, first_cluster, file_size))

    # Write FAT to disk
    for i in range(NOFS_FAT_SECTORS):
        lba = 1 + i
        disk[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE] = fat[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE]

    # --- Root directory at LBA 33..34 ---
    dir_area = bytearray(NOFS_DIR_SECTORS * SECTOR_SIZE)
    for idx, (fname, first_cluster, file_size) in enumerate(file_entries):
        if idx >= NOFS_DIR_ENTRIES:
            break
        entry = struct.pack('<24s', fname.encode()[:24])
        entry += struct.pack('<B', 0)         # attrs
        entry += struct.pack('<H', first_cluster)
        entry += struct.pack('<I', file_size)
        entry += b'\x00'                      # padding
        offset = idx * 32
        dir_area[offset:offset + len(entry)] = entry[:32]

    for i in range(NOFS_DIR_SECTORS):
        lba = NOFS_DATA_START - NOFS_DIR_SECTORS + i
        disk[lba * SECTOR_SIZE:(lba + 1) * SECTOR_SIZE] = dir_area[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE]

    # Write output
    with open(output_path, 'wb') as f:
        f.write(disk)

    print(f"Created {output_path} ({len(files)} files, {next_cluster - 2} clusters used)")
    for fname, _, size in file_entries:
        print(f"  {fname}: {size} bytes")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <pkg_directory> <output_image>")
        sys.exit(1)
    create_disk(sys.argv[1], sys.argv[2])

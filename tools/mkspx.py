#!/usr/bin/env python3
"""Create an SPX executable from a flat binary."""
import struct
import sys

def main():
    if len(sys.argv) not in (4, 5, 6):
        print("Usage: mkspx.py <input.bin> <app_name> <output.spx> [bss_size] [load_base]",
              file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1], 'rb') as f:
        data = f.read()

    bss_size = int(sys.argv[4], 0) if len(sys.argv) >= 5 else 0
    load_base = int(sys.argv[5], 0) if len(sys.argv) >= 6 else 0
    name = sys.argv[2][:11].ljust(12, '\0')[:12]

    hdr = struct.pack(
        '<IIIIII12sI',
        0x5350580A,  # magic
        3,            # version 3 adds load_base
        len(data),    # code_size
        bss_size,     # Size of uninitialised storage after the flat image.
        4096,         # stack_size
        0,            # exports_offset (at start of binary)
        name.encode('ascii', 'replace'),
        load_base,    # slot the image was linked for; 0 = any free slot
    )

    with open(sys.argv[3], 'wb') as f:
        f.write(hdr)
        f.write(data)

if __name__ == '__main__':
    main()

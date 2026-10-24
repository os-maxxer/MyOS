#!/usr/bin/env python3
"""Create an SPX executable from a flat binary."""
import struct
import sys

def main():
    if len(sys.argv) != 4:
        print("Usage: mkspx.py <input.bin> <app_name> <output.spx>", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1], 'rb') as f:
        data = f.read()

    name = sys.argv[2][:11].ljust(12, '\0')[:12]

    hdr = struct.pack(
        '<IIIIII12s',
        0x5350580A,  # magic
        1,            # version
        len(data),    # code_size
        0,            # bss_size (app uses .bss in loaded segment)
        4096,         # stack_size
        0,            # exports_offset (at start of binary)
        name.encode('ascii', 'replace')
    )

    with open(sys.argv[3], 'wb') as f:
        f.write(hdr)
        f.write(data)

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Create an SPX executable from a flat binary.

Besides the header (which the loader has always read), the package gets an
integrity footer holding a CRC-32 of the header and code, the package version
and the build stamp that produced it. The loader verifies the CRC when the
footer is present and ignores trailing bytes when it is not, so packages built
before this change still install.

The version and stamp default to the values the Makefile exports
(SPX_VERSION, SPX_BUILD_STAMP), so every package built by `make` can be traced
back to one kernel build.
"""
import binascii
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

FOOTER_MAGIC = 0x46585053
SPX_MAGIC = 0x5350580A
SPX_VERSION = 3


def footer(header_and_code, version, stamp):
    crc = binascii.crc32(header_and_code) & 0xFFFFFFFF
    return struct.pack('<II16s24s', FOOTER_MAGIC, crc,
                       version.encode('ascii', 'replace')[:15],
                       stamp.encode('ascii', 'replace')[:23])


def build(data, name, bss_size=0, load_base=0,
          version=os.environ.get('SPX_VERSION', '0'),
          stamp=os.environ.get('SPX_BUILD_STAMP', '')):
    name_field = name[:11].ljust(12, '\0')[:12]
    hdr = struct.pack(
        '<IIIIII12sI',
        SPX_MAGIC,   # magic
        SPX_VERSION,  # version 3 adds load_base
        len(data),    # code_size
        bss_size,     # uninitialised storage after the flat image
        4096,         # stack_size
        0,            # exports_offset (at start of binary)
        name_field.encode('ascii', 'replace'),
        load_base,    # slot the image was linked for; 0 = any free slot
    )
    body = hdr + data
    return body + footer(body, version, stamp)


def main():
    if len(sys.argv) not in (4, 5, 6):
        print("Usage: mkspx.py <input.bin> <app_name> <output.spx> [bss_size] [load_base]",
              file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1], 'rb') as f:
        data = f.read()

    bss_size = int(sys.argv[4], 0) if len(sys.argv) >= 5 else 0
    load_base = int(sys.argv[5], 0) if len(sys.argv) >= 6 else 0
    image = build(data, sys.argv[2], bss_size, load_base)

    with open(sys.argv[3], 'wb') as f:
        f.write(image)


if __name__ == '__main__':
    main()

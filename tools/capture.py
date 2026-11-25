#!/usr/bin/env python3
"""Boot Solis headlessly and capture reproducible screenshots.

Hand-taken screenshots are how the README drifted from the shipped image: a
picture of a build that nobody can point at. This driver boots the current
`build/solis.iso` plus `build/disk.img`, drives the guest through the QEMU
monitor, and writes its screenshots to a directory that survives reboots of
the build machine (per AGENTS.md, `$HOME/solis-test-artifacts` in WSL -- never
/tmp).

Steps are key sequences, so a flow that changes can be updated here instead of
being retyped into a terminal:

    python3 tools/capture.py                 # boot + sign in + desktop
    python3 tools/capture.py --flow keys.txt

A flow file holds one directive per line:

    wait 8            pause for N seconds
    key ret           press a named key (QEMU key names)
    type lui          type a literal string
    shot desktop      write <out>/desktop.png

The tool prints the path of every screenshot it writes.
"""
import argparse
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
import zlib

DEFAULT_ART = os.path.join(os.path.expanduser("~"), "solis-test-artifacts")

# Setup wizard: Enter, Standard edition, Enter, account name, password, Enter
# through the remaining steps; then sign in with the same password.
DEFAULT_FLOW = [
    ("wait", 9),
    ("key", "ret"),
    ("key", "2"),
    ("key", "ret"),
    ("type", "lui"),
    ("key", "tab"),
    ("type", "solis"),
    ("key", "ret"),
    ("key", "ret"),
    ("key", "ret"),
    ("wait", 6),
    ("type", "solis"),
    ("key", "ret"),
    ("wait", 8),
    ("shot", "desktop"),
]


def ppm_to_png(src, dst):
    """Convert a binary PPM to PNG.

    QEMU's screendump writes PPM when it was built without PNG support, so a
    file called .png is often really a P6 image and most viewers refuse it.
    """
    with open(src, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise ValueError("%s is not a binary PPM" % src)

    fields = []
    pos = 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    width, height, maxval = fields
    if maxval != 255:
        raise ValueError("unsupported PPM maxval %d" % maxval)

    stride = width * 3
    raw = bytearray()
    for row in range(height):
        off = pos + row * stride
        raw += b"\x00" + data[off:off + stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    with open(dst, "wb") as handle:
        handle.write(png)
    os.unlink(src)


def wait_for_socket(path, timeout=30.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return True
        time.sleep(0.2)
    return False


class Monitor:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buf = b""
        self.command("")

    def command(self, text):
        self.sock.sendall((text + "\n").encode("ascii"))
        time.sleep(0.15)
        try:
            self.sock.settimeout(0.4)
            self.buf = self.sock.recv(65536)
        except (socket.timeout, OSError):
            pass
        return self.buf.decode("latin1", "replace")

    def key(self, name):
        self.command("sendkey %s" % name)

    def type_text(self, text):
        for ch in text:
            self.key(KEYMAP.get(ch, ch))

    def screendump(self, path):
        ppm = path + ".ppm"
        self.command("screendump %s" % ppm)
        time.sleep(1.0)
        ppm_to_png(ppm, path)


# QEMU sends key *names*; letters and digits are their own names.
KEYMAP = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
    "_": "shift-minus",
}


def run_flow(monitor, flow, out_dir):
    shots = []
    for action, value in flow:
        if action == "wait":
            time.sleep(float(value))
        elif action == "key":
            monitor.key(value)
        elif action == "type":
            monitor.type_text(value)
        elif action == "shot":
            path = os.path.join(out_dir, "%s.png" % value)
            monitor.screendump(path)
            shots.append(path)
            print("capture: %s" % path)
        else:
            raise SystemExit("capture: unknown directive %r" % action)
    return shots


def parse_flow(path):
    flow = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            action, _, value = line.partition(" ")
            flow.append((action, value.strip()))
    return flow


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--out", default=DEFAULT_ART,
                        help="screenshot directory, default %s" % DEFAULT_ART)
    parser.add_argument("--flow", default="",
                        help="key script to run instead of the boot flow")
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    args = parser.parse_args()

    if not shutil.which(args.qemu):
        print("capture: %s not found; run this inside WSL" % args.qemu,
              file=sys.stderr)
        return 2

    iso = os.path.join(args.build_dir, "solis.iso")
    disk = os.path.join(args.build_dir, "disk.img")
    for path in (iso, disk):
        if not os.path.isfile(path):
            print("capture: %s is missing; run make first" % path,
                  file=sys.stderr)
            return 2

    os.makedirs(args.out, exist_ok=True)
    monitor_path = os.path.join(args.out, "monitor.sock")
    serial_path = os.path.join(args.out, "serial.log")
    if os.path.exists(monitor_path):
        os.unlink(monitor_path)

    flow = parse_flow(args.flow) if args.flow else DEFAULT_FLOW
    proc = subprocess.Popen([
        args.qemu, "-vga", "std", "-m", "256M",
        "-cdrom", iso,
        "-drive", "file=%s,format=raw,if=ide" % disk,
        "-display", "none",
        "-monitor", "unix:%s,server,nowait" % monitor_path,
        "-serial", "file:%s" % serial_path,
        "-nic", "user,model=rtl8139",
    ])

    try:
        if not wait_for_socket(monitor_path):
            print("capture: the QEMU monitor socket never appeared",
                  file=sys.stderr)
            return 1
        monitor = Monitor(monitor_path)
        run_flow(monitor, flow, args.out)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("capture: serial log %s" % serial_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())

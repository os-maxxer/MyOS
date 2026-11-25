#!/usr/bin/env python3
"""Prove that the shipped artifacts are the ones that were just built.

A build can look complete and still be wrong: `build/solis-uefi.vdi` once
carried a 25 KB `ck3.spx` while the freshly built package was 89 KB, so the
VirtualBox guest ran a different game from the one that had been tested. File
timestamps and exit codes cannot catch that, so this tool checks content:

  1. every `pkg/<name>/<name>.spx` byte-matches its `build/spx/<name>.spx`
     (catches a stale copy left in the package directory);
  2. `solis.iso` and `solis-uefi.vdi` contain the kernel's build id
     (catches an image built from an older kernel);
  3. `disk.img` and `solis-uefi.vdi` contain every packaged `.spx` verbatim
     (catches an image built from older packages).

`disk.img` holds packages only and the ISO holds the kernel only, by design,
so neither is required to satisfy both checks.

Usage:
    python3 tools/verify_images.py [--build-dir build]

Exit status is 0 only when every check passes.
"""
import argparse
import hashlib
import os
import re
import sys

STAMP_RE = re.compile(rb"\d{8}-\d{6}-[0-9A-Za-z]+")


def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def kernel_stamp(path):
    """The build id compiled into the kernel image."""
    found = STAMP_RE.search(read_bytes(path))
    return found.group(0).decode("ascii", "replace") if found else None


def package_pairs(build_dir):
    """Yield (name, build_path, pkg_path) for each packaged app."""
    spx_dir = os.path.join(build_dir, "spx")
    if not os.path.isdir(spx_dir) or not os.path.isdir("pkg"):
        return
    for name in sorted(os.listdir("pkg")):
        pkg_path = os.path.join("pkg", name, "%s.spx" % name)
        build_path = os.path.join(spx_dir, "%s.spx" % name)
        if os.path.isfile(pkg_path) and os.path.isfile(build_path):
            yield name, build_path, pkg_path


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()

    build = args.build_dir
    kernel = os.path.join(build, "solis.kernel")
    if not os.path.isfile(kernel):
        print("verify: %s is missing; run make first" % kernel, file=sys.stderr)
        return 2

    stamp = kernel_stamp(kernel)
    if not stamp:
        print("verify: no build id in %s; rebuild so the stamp is compiled in"
              % kernel, file=sys.stderr)
        return 2
    print("verify: kernel build id %s" % stamp)

    failures = []

    # 1. The repository package directory matches the freshly built packages.
    pairs = list(package_pairs(build))
    if not pairs:
        print("verify: no built packages under %s/spx" % build, file=sys.stderr)
    for name, build_path, pkg_path in pairs:
        fresh = read_bytes(build_path)
        shipped = read_bytes(pkg_path)
        if fresh != shipped:
            failures.append(
                "pkg/%s/%s.spx is stale: %d bytes on disk, %d in the build"
                % (name, name, len(shipped), len(fresh)))
        else:
            print("  ok   pkg/%-14s %s" % (
                name + ".spx", hashlib.sha256(fresh).hexdigest()[:12]))

    # 2 and 3. Artifact contents.
    artifacts = [
        ("solis.iso", os.path.join(build, "solis.iso"), True, False),
        ("disk.img", os.path.join(build, "disk.img"), False, True),
        ("solis-uefi.vdi", os.path.join(build, "solis-uefi.vdi"), True, True),
    ]
    for label, path, wants_kernel, wants_packages in artifacts:
        if not os.path.isfile(path):
            failures.append("%s is missing; build it before shipping" % label)
            continue
        blob = read_bytes(path)
        if wants_kernel:
            if stamp.encode("ascii") not in blob:
                failures.append("%s does not contain kernel build id %s"
                                % (label, stamp))
                continue
            print("  ok   %-15s kernel %s" % (label, stamp))
        if not wants_packages:
            continue
        for name, _, pkg_path in pairs:
            if read_bytes(pkg_path) not in blob:
                failures.append("%s does not contain the current "
                                "pkg/%s/%s.spx" % (label, name, name))
                continue
            print("  ok   %-15s package %s" % (label, name))

    if failures:
        print("", file=sys.stderr)
        for line in failures:
            print("verify: FAIL %s" % line, file=sys.stderr)
        print("verify: %d problem(s); these artifacts do not match the "
              "current build" % len(failures), file=sys.stderr)
        return 1

    print("verify: all artifacts match build %s" % stamp)
    return 0


if __name__ == "__main__":
    sys.exit(main())

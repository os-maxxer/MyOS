# Third-party notices

Solis itself is licensed under the Apache License 2.0 (see `LICENSE`). The
Extra edition bundles the following third-party software.

## Doom / doomgeneric

The Doom source code in `third_party/doom/` is derived from id Software's Doom
and the `doomgeneric` platform port. It is distributed under the **GNU General
Public License, version 2**. The full license text is in
`third_party/doom/LICENSE`, and the upstream README is in
`third_party/doom/README.upstream.md`.

Because Solis links this GPL-2.0 code into the Extra-edition kernel image and
the Doom SPX application, redistribution of those Extra-edition binaries is
subject to the GPL-2.0. The complete corresponding source is this repository.

## Shareware Doom Episode 1 data

`third_party/doom/doom1.wad` is the freely redistributable shareware Episode 1
data for Doom, originally published by id Software. It may be copied and
distributed for personal use; it is not a substitute for the commercial game.
Do not add commercial `doom.wad` data to this repository.

## Other components

Solis uses GRUB 2 for booting. GRUB is distributed under the GNU GPL v3; it is
installed as a separate bootloader and is not linked into the Solis kernel.

# Solis 100-change pass

This pass covers the HD desktop, the expanded Browser settings page, selectable
web-search providers, the new picture decoders, and the fixes found while
testing them. The list is split into 50 large changes and 50 small changes.

Verification labels:

- **VM** — exercised in the VirtualBox guest.
- **Host** — exercised by the host-side image-decoder fixtures.
- **Build** — compiled, linked, and checked against the Browser SPX slot.
- **Visual** — the rendered page was inspected from a guest screenshot.

The guest has no mouse injection, so link controls were exercised through their
equivalent `solis://` URLs and the rendered settings page rather than by
clicking each control. Every decoder format in the gallery was rendered in the
guest.

Build used for the guest checks: `20261116-045621`. Browser image: 162,904 bytes
of text plus 2,892,384 bytes of BSS, 3,055,288 bytes total inside the 3 MiB SPX
slot.

## Large changes (1–50)

| # | Change | Verification |
|---|--------|--------------|
| 1 | Request a 1920x1080x32 VBE framebuffer from Multiboot2 | VM |
| 2 | BIOS GRUB configuration selects the HD mode | VM |
| 3 | UEFI GRUB configuration selects the HD mode | VM |
| 4 | Frame staging buffer enlarged for 1080p | VM |
| 5 | Desktop background cache enlarged for 1080p | Visual |
| 6 | Wallpaper decoder accepts 1920x1080 sources | Build |
| 7 | Bilinear source-to-thumbnail scaling for large images | Host |
| 8 | Browser window enlarged to 1000x700 | Visual |
| 9 | Six decoded picture slots in the page cache | Visual |
| 10 | Thumbnails enlarged to 180x135 | Visual |
| 11 | GIF first-frame decoder | Host, Visual |
| 12 | GIF LZW decompressor | Host, Visual |
| 13 | GIF interlaced-row handling | Build |
| 14 | GIF transparent-color handling | Build |
| 15 | ICO container decoder | Host, Visual |
| 16 | PNG-compressed ICO entries | Build |
| 17 | 8, 24, and 32-bit ICO entries | Build |
| 18 | Best-size ICO entry selection | Build |
| 19 | 8-bit BMP palette decoder | Host, Visual |
| 20 | 16-bit BMP RGB555/565 decoder | Host |
| 21 | 32-bit BMP RGB/A decoder | Host, Visual |
| 22 | BMP bitfield masks | Build |
| 23 | Large BMP source dimensions sampled to thumbnails | Build |
| 24 | PNG source cap raised to 320x256 | Build |
| 25 | Format sniffing and GIF/ICO labels | Host |
| 26 | Expanded Browser settings dashboard | Visual |
| 27 | Selectable primary web-search API | VM |
| 28 | Bing RSS provider | VM |
| 29 | DuckDuckGo provider | VM |
| 30 | FreeSerp provider | VM |
| 31 | DuckDuckGo off mode | VM |
| 32 | DuckDuckGo fallback mode | Build |
| 33 | DuckDuckGo merged-results mode | Build |
| 34 | Automatic Bing fallback | Build |
| 35 | DuckDuckGo supplementation after another provider | Build |
| 36 | FreeSerp last-resort fallback | Build |
| 37 | DigiCert Global Root G3 trust anchor for Bing | VM |
| 38 | Browser settings persistence in `/home/browser.cfg` | Build |
| 39 | Reset all Browser settings | Build |
| 40 | Clear Browser history action | Build |
| 41 | Network/provider/picture status summary | Visual |
| 42 | Picture loading on/off setting | VM |
| 43 | JavaScript on/off setting | Build |
| 44 | F11 opens Browser directly | VM |
| 45 | F12 opens Settings directly | VM |
| 46 | On-disk picture-format gallery | Visual |
| 47 | SOLFS package support for GIF and ICO assets | Visual |
| 48 | Test-search links on the API settings card | Build |
| 49 | Correct 8-bit palette reserved-byte handling | Host, Visual |
| 50 | Keep the enlarged Browser inside the 3 MiB slot budget | Build |

## Small changes (51–100)

| # | Change | Verification |
|---|--------|--------------|
| 51 | Settings base-URL builder preserves current choices | Build |
| 52 | Settings choice-URL builder appends one change safely | Build |
| 53 | Selected-option marker in settings | Visual |
| 54 | Selected-option highlight style | Visual |
| 55 | Dark-theme settings option text | Build |
| 56 | Shared settings card heading renderer | Visual |
| 57 | Theme option descriptions | Visual |
| 58 | Web API option descriptions | Visual |
| 59 | DuckDuckGo option descriptions | Build |
| 60 | Picture option descriptions | Build |
| 61 | JavaScript option descriptions | Build |
| 62 | Human-readable API names | Visual |
| 63 | Two quick test-search links | Build |
| 64 | Picture-gallery link from settings | Build |
| 65 | Back-to-start link | Build |
| 66 | Clear-history link | Build |
| 67 | Reset-settings link | Build |
| 68 | Connected/disconnected network badge | Visual |
| 69 | Primary API status line | Visual |
| 70 | Picture-loading status line | Visual |
| 71 | Dedicated `/home/browser.cfg` constants | Build |
| 72 | Settings load parser | Build |
| 73 | Settings save serializer | Build |
| 74 | Settings defaults function | Build |
| 75 | Theme query parameter handling | Build |
| 76 | API query parameter handling | VM |
| 77 | DuckDuckGo query parameter handling | VM |
| 78 | Pictures query parameter handling | VM |
| 79 | JavaScript query parameter handling | Build |
| 80 | Explicit `WEB_API_*` enum | Build |
| 81 | Explicit `DDG_*` mode enum | Build |
| 82 | Bing RSS item parser | VM |
| 83 | RSS result-count bound | Build |
| 84 | RSS text cleanup | Build |
| 85 | Reused URL query encoder for providers | Build |
| 86 | `solis://search` provider parsing | VM |
| 87 | Bing endpoint helper | VM |
| 88 | FreeSerp fallback helper | VM |
| 89 | FreeSerp JSON result parsing | VM |
| 90 | Treat BMP palette byte four as reserved | Host |
| 91 | Document why palette byte four is ignored | Build |
| 92 | Explain the G3 trust-root requirement in source | Build |
| 93 | Add the G3 subject DN array | VM |
| 94 | Add the G3 P-384 public key array | VM |
| 95 | Add the G3 trust-anchor entry | VM |
| 96 | F11 launch trace in the serial log | VM |
| 97 | F12 launch trace in the serial log | Build |
| 98 | Readable gallery page structure | Visual |
| 99 | Six small packaged gallery images | Visual |
| 100 | README release notes and this verification ledger | Build |

## Test evidence

- VirtualBox serial log confirmed `[FB] ... w=1920 h=1080 bpp=32 pitch=7680`.
- Settings screenshots showed Bing RSS selected, pictures off, and JavaScript
  off after navigating to the corresponding settings URL.
- Bing, DuckDuckGo, and FreeSerp searches each returned a populated results
  page with no kernel fault. Bing initially failed with BearSSL error 62
  (`BR_ERR_X509_NOT_TRUSTED`); adding DigiCert Global Root G3 fixed it.
- The gallery rendered BMP 8-bit, BMP 24-bit, BMP 32-bit, PNG, ICO, and GIF.
- Host fixtures decoded BMP 8/16/24/32, PNG, ICO, and GIF with
  `failures=0`, including the corrected 8-bit palette colors.
- Browser link check: 3,055,288 bytes total, 90,888 bytes below the 3 MiB
  slot limit.

## Follow-up: image types and source sizes

This follow-up adds formats and raises the source ceilings requested after the
100-change pass.

| Change | Verification |
|--------|--------------|
| TGA truecolor, grayscale, colormapped, uncompressed and RLE decoding | Host, Visual |
| TGA top/bottom and left/right origins | Host (top-left), Build (reverse) |
| Binary PPM (P6), PGM (P5), and PBM (P4) decoding | Host, Visual |
| 16-bit PPM/PGM samples | Host |
| QOI RGB/RGBA decoding with index, diff, luma, and run packets | Host (RGB/run), Build (other chunks) |
| XBM monochrome C-source decoding | Host, Visual |
| BMP 1-bit and 4-bit palette decoding | Host |
| PNG source limit raised to 384x256 | Host |
| BMP and TGA source limit raised to 8192x8192 | Build |
| PNM source limit raised to 8192x8192 | Build |
| GIF source limit raised to 4096x4096 | Build |
| ICO source limit raised to 1024x1024 | Build |
| New formats packaged into SOLFS and shown in `picture-formats-2.html` | Visual |
| Settings page lists formats and source limits | Build |

Host fixtures decoded TGA (plain, RLE, grayscale, 16-bit, 32-bit, colormap),
PPM, PGM, PBM, QOI, XBM, BMP 1/4-bit, and a 384x256 PNG with zero failures.
The VirtualBox guest rendered TGA, PPM, PGM, PBM, QOI, and XBM on the second
gallery page without a fault.

## Follow-up: SOLPKG, packages, and UTF-8 text

This pass makes the package manager usable end to end, ships every built-in app
as a package, adds a publish command, and replaces the ASCII-only text path.

## Package manager

| Change | Effect |
|--------|--------|
| SPX header v3 with `load_base` | A package records the slot it was linked for |
| Slot-pinned installs | Fixed-address code lands in its own slot or is refused |
| `SPX_ERR_BUSY` / `SPX_ERR_BASE` | SOLPKG names the app occupying the slot |
| Size validation | Code and BSS must fit the 3 MiB slot; no silent truncation |
| `repo.json` parsing | `repo-list`, `fetch`, and `search` read real metadata |
| `fetch <name>` | Uses the manifest's `file` path instead of `/<name>/<name>.npx` |
| `.spx` everywhere | `local-install` and `fetch` no longer look for `.npx` |
| 1 MiB package buffer | Packages up to a slot-size image install from disk |
| 1 MiB HTTP receive buffer | The kernel client no longer truncates downloads at 8 KiB |
| `upgrade` | Reinstalls every package that is currently installed |
| Self-replacement guard | SOLPKG refuses to overwrite its own slot |
| Running-app guard | An app must be closed before its slot is upgraded |
| Rolling console | Output no longer stops after 32 lines |

## Packages and publishing

All thirteen built-in apps are packaged into `pkg/<name>/<name>.spx`, each
linked at its own slot base and listed in `pkg/repo.json` with a version,
description, slot, file path, and the legacy `.npx` name it replaces.

`make commit` runs `tools/npk_commit.py`, which uploads every built `.spx` to
`os-maxxer/NPK` through the GitHub Contents API, deletes the matching `.npx`
files, and refreshes `repo.json` in the repository. `make commit-dry` shows the
same plan without writing. Authentication comes from `GITHUB_TOKEN`, `GH_TOKEN`,
or `gh auth token`; no credential is stored in the project.

## Text stack

| Change | Effect |
|--------|--------|
| UTF-8 decoder | Validates sequences, rejects overlong/surrogate input, consumes one byte on error |
| Codepoint layout | `graphics_text_width` and the Browser layout advance by glyph |
| Latin composition | Accented Latin-1 and Latin Extended-A draw from base glyph + accent |
| Glyph set | Punctuation, quotes, dashes, currency, math, arrows, Greek, Cyrillic |
| Double-width scripts | CJK, Hangul, fullwidth, and emoji occupy two cells |
| Combining marks | U+0300–U+036F overlay the previous cell instead of consuming one |
| Replacement box | Unknown codepoints render as tofu instead of `?` |
| HTML entities | Named and numeric entities decode to UTF-8, not to `' '` |
| JSON `\uXXXX` | Search results keep their non-Latin characters |
| Tabs | Advance to the next four-cell stop |

## Verification

- Kernel, every SPX app, all thirteen packages, and the ISO build with `-Werror`.
- `pkg/*/*.spx` headers checked: version 3, correct base per slot, code + BSS
  inside 3 MiB. Browser: 173,080 bytes of code plus 2,957,920 of BSS,
  3,131,000 total, 14,728 bytes below the limit.
- Host UTF-8 test: ASCII, Latin-1, three- and four-byte sequences, overlong,
  stray continuation, truncated input, widths, tabs, combining marks, clipping,
  and encoding round-trip — all passed.
- Host manifest test: `pkg/repo.json` parses to 13 entries in order with the
  expected names, slots, versions, and `.spx` paths — passed.
- Host fetch harness with the real `cmd_fetch`: manifest lookup, path build,
  a 173 KB Browser download, the install call, and the unknown-package error
  all behaved correctly — passed.
- `tools/npk_commit.py --dry-run` planned all 13 uploads, the `.npx`
  deletions, and the manifest update for `os-maxxer/NPK`.
- Not run in the guest in this pass: the browser/text rendering and a live
  `fetch` against the test server.

## Boot verification (VirtualBox, UEFI)

Booting the rebuilt image exposed three defects the host tests could not reach:

- `tests/user/user_test.c` asserted `repo.json`'s exact byte length, so the
  expanded 13-package manifest failed the built-in task self-test and the
  kernel panicked before the desktop. The test now reads to EOF and checks
  width, offset advance and EOF stability instead of one fixed size.
- SOLFS had 32 root directory entries, exactly as many files as the image
  ships, so the installer could not create `/settings`. The root directory is
  now 64 entries over 4 sectors and `SOLFS_VERSION` is 2; `tools/mksolfs.py`
  and `kernel/solfs.c` agree on the new layout and old images are rejected.
- `b_utf8_trim` looked at the byte *past* the string, so a word could lose its
  last character ("Curly" drew as "Curl") when that stale byte looked like a
  UTF-8 lead byte. It now trims only a genuinely partial trailing sequence.

Verified in the guest: setup wrote the account and reached the desktop;
`repo add http://10.0.2.2:8000` and `repo-list` read the manifest over HTTP;
`purge tetris` then `fetch tetris` downloaded and installed the package into
slot 8 and the installed app launched. Browser loaded a page carrying é, ñ,
è, û, ü, Å, ö, curly quotes, an em dash, an ellipsis, entity forms and Greek,
and all of them rendered.

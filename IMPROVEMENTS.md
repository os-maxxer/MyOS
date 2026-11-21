# Solis 2.1 / Bedrock — release notes

Solis 2.1 is the Bedrock foundation pass: the release focuses on keeping a
working system intact while its edition and app set change.

## Crisp display, selectable search, and 100-change pass

This pass raises the display to **1920x1080** and adds a 100-item ledger of
large and small changes. See **[CHANGES-100.md](CHANGES-100.md)** for the full
list and its verification status.

1. **1080p desktop** — BIOS and UEFI GRUB request 1920x1080x32, and the frame
   staging, desktop background cache, and wallpaper loader accept the larger
   frame. The serial boot line reports `w=1920 h=1080 bpp=32`.
2. **Larger, sharper Browser** — the window is 1000x700 with six 180x135
   thumbnails and bilinear downscaling for large pictures.
3. **GIF and ICO** — Browser decodes the first GIF frame (including LZW,
   interlacing, and transparency) and ICO files (8/24/32-bit and
   PNG-compressed entries), in addition to its existing BMP, PNG, and JPEG
   support. BMP now covers 8/16/24/32-bit files and bitfields.
4. **Expanded Browser settings** — Appearance, primary web API (Bing RSS,
   DuckDuckGo, or FreeSerp), DuckDuckGo mode, pictures, JavaScript, network
   status, reset, and clear-history controls all persist in
   `/home/browser.cfg`.
5. **Provider fallback chain** — a failed primary provider falls through Bing
   and DuckDuckGo to FreeSerp. Bing initially failed certificate validation;
   adding the DigiCert Global Root G3 trust anchor fixed it.
6. **8-bit BMP fix** — the BMP palette's fourth byte is reserved, not alpha;
   treating it as alpha blended ordinary 8-bit BMPs into white.
7. **Picture gallery and launchers** — `file:///picture-formats.html` loads a
   packaged gallery of six formats. `F11` opens Browser and `F12` opens
   Settings directly.

## Additional image formats and source sizes

1. **TGA** — truecolor, grayscale, colormapped, uncompressed, and RLE TGA
   files, including top/bottom and left/right origins.
2. **Netpbm** — binary PPM (P6), PGM (P5), and PBM (P4), including 16-bit
   PPM/PGM samples and packed monochrome PBM rows.
3. **QOI** — the Quite OK Image format with RGB/RGBA chunks, index runs,
   differences, luma, and run-length packets.
4. **XBM** — monochrome X BitMap C-source files, parsed without a full image
   allocation.
5. **BMP 1-bit and 4-bit** — palette-packed monochrome and 16-color BMPs, in
   addition to the existing 8/16/24/32-bit support.
6. **Larger source limits** — PNG now accepts 384x256 (up from 320x256), TGA
   and PNM accept 8192x8192, BMP accepts 8192x8192, GIF accepts 4096x4096, and
   ICO accepts 1024x1024. JPEG retains its 16384-pixel ceiling.
7. **Two galleries** — `picture-formats-2.html` demonstrates TGA, PPM, PGM,
   PBM, QOI, and XBM alongside the original BMP/PNG/JPEG/ICO/GIF gallery.

The host fixture suite decodes all of the old and new formats with
`failures=0`, including plain and RLE TGA, 1-bit/4-bit BMP, and a 384x256
PNG. The VirtualBox guest renders all six images on the second gallery page.

## Browser, audio, and package-room pass

1. **Section links** — Browser preserves URL fragments, scrolls to matching
   `id` anchors, and applies Wikipedia `#section` fragments after the article
   finishes loading. Same-page links update the address without a refetch.
2. **Larger JavaScript engine** — the bounded interpreter now has more nodes,
   environments, variables, functions, events, strings, and arrays; added
   `setTimeout`/`setInterval`, `querySelectorAll`, `createElement`,
   `appendChild`, `removeChild`, `remove`, `parentNode`/`children`/`next`, and
   `classList`, plus string, array, and `Math` helpers. Timers run from the
   Browser tick without blocking input.
3. **3 MiB app slots** — SPX slots grew from 2 MiB to 3 MiB, with app bases
   3 MiB apart and the kernel's user-image and frame staging areas moved below
   the Doom heap so larger apps load without overlapping staging memory.
4. **AC'97 sound** — a PCI AC'97 PCM-out driver initializes the codec, builds a
   48 kHz stereo 16-bit buffer, and plays a test tone. Settings gained a Sound
   tab with driver status and mouse or Enter activation.
5. **Wikipedia rendering** — articles are fetched through the lightweight
   `action=render` endpoint and styled with a Wikipedia-like theme, with a
   logo, wordmark, search field, article tabs, and language link. Infobox and
   article pictures load through bounded thumbnails. Large documents skip page
   stylesheet matching, which keeps long articles responsive instead of
   spending minutes in selector matching.
6. **GitHub search and repositories** — `gh`/`github` searches use the GitHub
   repository API, and repository addresses render a themed page with the
   owner avatar, name, description, stars, forks, issues, language, navigation,
   About panel, and README instead of loading GitHub's script-heavy page. A
   synthetic dark dashboard supplies the familiar GitHub home, header, and
   search field.
7. **Downloads** — `Save`/`Ctrl+S` and links with the `download` attribute write
   pages or files to `/home/downloads`, with safe filenames and numeric
   suffixes when a name already exists.
8. **Browser settings and search safety** — a toolbar Settings page stores
   Light/Dark mode and three DuckDuckGo choices in `/home/browser.cfg`: off,
   fallback only, or results merged into every web search. Wikipedia and GitHub
   search forms now submit to Solis result pages rather than loading the full
   source sites, and form/DOM parent walks are depth-limited so a malformed
   tree cannot freeze the mouse and desktop.
9. **CSS coverage** — flex and grid containers fall back to block layout,
   `visibility`, widths, `max-width`, automatic margins, and site themes for
   Wikipedia and GitHub cover more of their familiar presentation.
10. **Search freeze and connection retry** — TCP checksums now use a
    packet-sized scratch buffer instead of a 512-byte stack buffer. TLS writes
    are sent in 1 KiB chunks, so a larger search request could overwrite the
    checksum function's return address and halt the kernel; this was the cause
    of the Wikipedia search freeze. Browser also retries a failed DNS lookup or
    connection once before showing a connection error, so a transient loss no
    longer requires a manual refresh.

## Bedrock 2.1 foundation pass

1. **Restart-safe Edition Manager** — **Settings > Edition** queues Minimal,
   Standard, or Extra without reinstalling or erasing the account and files.
2. **Pending edition state** — the request is stored separately from the
   account, so the running desktop is never unloaded underneath an open
   window. The next boot applies it before the desktop starts.
3. **Cancellable changes** — selecting the current edition clears a queued
   change, and the tab shows both the current and next-start editions.
4. **Bedrock identity** — the release line is now **Solis 2.1 / Bedrock** in
   setup, sign-in, the start menu, and About.
5. **PCnet-FAST III networking** — Solis also drives the AMD PCnet-FAST III
   (Am79C973) in 32-bit software style, so VirtualBox NAT works with its
   PCnet adapter as well as the existing RTL8139.
6. **Small HTTP/HTTPS and local-file browser** — Browser is a thirteenth SPX
   app. It renders HTTP, HTTPS, and `file://` HTML as a DOM-lite tree with
   headings, paragraphs, CSS `<style>` rules, inline colors, backgrounds,
   links, buttons, lists, rules, and BMP/PNG/JPEG images; it scrolls with the
   wheel or arrow keys and follows the online-only launch rule. A bounded,
   integer-only JavaScript subset handles variables, functions, loops, arrays,
   `document` queries, `textContent`/`innerHTML`, style changes,
   `onclick`/`addEventListener`, `document.write`, and `window.onload`. Images
   load one per tick through a four-thumbnail cache; a baseline JPEG decoder
   handles grayscale and YCbCr images at 4:4:4, 4:2:2, and 4:2:0 sampling
   without a source-sized buffer. The CSS engine handles tag, class, id, and
   descendant selectors, the cascade, colors, backgrounds, display, margins,
   padding, borders, radius, font weight, alignment, and underline. Typing
   plain words such as `wikipedia` in the address bar opens a Solis results page
   backed by a keyless web search API, with DuckDuckGo and Wikipedia links on
   the results page.
   The start page has a colorful masthead, search field, and shortcut tiles.
   HTTPS uses a kernel TLS 1.2 client with X.509 validation against a bundle of
   ISRG, DigiCert, Google Trust Services, and Amazon roots, and HTTP requests
   send browser-like headers, so more sites load. A sample local page,
   `/welcome.html`, demonstrates CSS cards, a JPEG photograph, search, and an
   interactive button; Editor gained a real HTML mode with tag, attribute,
   string, comment, and entity highlighting.

## Daybreak 2.0 / 50-change pass

The 2.0 release bundled the requested 50 modernizing, maturing, and branding
changes. Every item below is implemented in the source tree.

## Brand and product maturity (1–10)

1. **Central brand header** — `include/solis/brand.h` is the single source for
   the product name, version, release name, and tagline.
2. **Solis 2.0 “Daybreak” release identity** — setup, login, boot, menu, and
   About now agree on one release line.
3. **New tagline** — “A calmer place to compute” replaces generic hobby-OS
   copy across the shell and installer.
4. **Branded boot screen** — the boot report carries the release name and
   tagline instead of only a version number.
5. **Branded sign-in** — the login screen uses the release mark and tagline.
6. **Branded installer** — the setup shell, sidebar, footer, and summary share
   the Daybreak identity.
7. **Version in the start menu** — the menu header shows the current release
   rather than a hard-coded “Solis OS”.
8. **Branded About tab** — Settings reports release, edition, session type,
   GUI framework, and Doom licensing.
9. **Branded README** — features, apps, setup, editions, guest access, Doom
   controls, and licensing are documented for this release.
10. **Third-party notices** — Doom GPL-2.0, the shareware WAD, and GRUB are
    attributed in `THIRD_PARTY_NOTICES.md`.

## Desktop polish (11–20)

11. **Edition-aware app launching** — apps outside the installed edition
    cannot be launched from icons, pins, or the menu.
12. **Edition-aware start menu** — unavailable rows are omitted rather than
    shown as dead entries.
13. **Empty menu sections disappear** — a section with no available apps is
    hidden instead of leaving a stranded heading.
14. **Games section** — Tetris, Solis Blocks, and Doom now live under a clear
    Games heading.
15. **Edition badge in the start menu** — the header shows MIN, STD, or EXTRA
    for local sessions.
16. **Guest tray badge** — the tray marks guest sessions clearly.
17. **Guest menu badge** — the start-menu header marks guest sessions.
18. **Guest session state** — a guest session survives login without exposing
    account controls and is handed to the desktop explicitly.
19. **Edition-aware desktop icons** — the Doom icon only appears in Extra, and
    the desktop fills its game slot with Calculator where that app exists.
20. **Fixed GUI cache region** — large wallpaper/browser caches live in a
    reserved `.gui_cache` region so the kernel stays comfortably inside its
    SPX memory budget.

## Required installer editions (21–30)

21. **Required Edition step** — insert a dedicated step immediately after
    Welcome; Continue cannot advance without an explicit selection.
22. **Extra is preselected** — the image boots with Extra highlighted and
    described, but still requires confirmation.
23. **Explicit confirmation gate** — the primary button stays disabled and
    changes from “Select an edition” to “Continue” only after a choice.
24. **Edition cards** — each profile has a number, name, description, and
    LEAN / BALANCED / RECOMMENDED tag.
25. **Minimal edition** — installs only the core desktop apps: Helios, Notes,
    Settings, Files, and Task Manager.
26. **Standard edition** — installs every built-in app except Doom.
27. **Extra edition** — installs every app plus the playable Doom port.
28. **Guest installation** — **Continue as guest** creates a passwordless
    persisted guest account and still goes through the required edition step.
29. **Five-step onboarding** — Welcome, Edition, Account, Look & feel, Finish
    with a persistent progress rail.
30. **Edition summary** — the finish screen restates the selected edition and
    whether the install is local or guest.

## Guest access (31–35)

31. **Guest account persistence** — guest status is stored in the account
    record, not just remembered for one boot.
32. **Guest button on setup** — a visible guest path sits below the account
    fields without colliding with the error or footer text.
33. **Guest button on sign-in** — **Continue as guest** is always available on
    the login screen, not only for guest-only installs.
34. **Passwordless guest entry** — guest login accepts an empty password while
    still rejecting it for local accounts.
35. **Keyboard guest access** — Tab reaches the guest button and Enter starts a
    guest session, so the path is not mouse-only.

## Playable Doom (36–45)

36. **Real Doom source** — `doomgeneric` is compiled for Solis rather than a
    placeholder mini-game.
37. **Embedded shareware WAD** — the 4.1 MB `doom1.wad` is packed into kernel
    `.rodata` and exposed through the syscall table.
38. **Twelfth SPX app** — Doom is built, packaged, loaded, and scheduled like
    the other desktop apps.
39. **Doom desktop integration** — desktop icon, Games menu entry, title bar,
    taskbar button, and launch path.
40. **Fullscreen play** — launching Doom switches the game to a fullscreen
    presentation.
41. **Doom controls** — arrows, Ctrl fire, Shift run, Alt strafe, Enter use, and
    Esc for the game menu.
42. **Esc passthrough** — the desktop no longer consumes Esc while Doom is the
    active game.
43. **Scaled framebuffer blit** — a nearest-neighbour `blit_rgb_scaled` syscall
    lets the 320×200 game fill the 1024×768 desktop.
44. **Failure containment** — Doom aborts through its own `DG_Error` longjmp
    path instead of freezing the kernel when the WAD or a subsystem fails.
45. **SSE/FPU enabled at boot** — CR4 and `fninit` setup allow the Doom build to
    use SSE2 math while the kernel itself remains `-mno-sse`.

## Install and platform maturity (46–50)

46. **Edition pruning at install time** — apps outside the selected edition are
    uninstalled and their staged package files removed.
47. **Legacy account records still load** — v1 setup records are read with
    Extra edition defaults instead of forcing a reinstall.
48. **Consistent account policy** — setup, guest, and password changes share
    the same validation and salted-hash path.
49. **Documented recovery and hardware limits** — README calls out UEFI-only
    VDI requirements, BIOS media, and unsupported storage rather than implying
    universal hardware support.
50. **Release artifact discipline** — `tools/ship.sh` preserves the VirtualBox
    UEFI disk UUID, records build IDs and hashes, and refuses to overwrite
    images while the test VM is running.

## Verification notes

- `bash tools/ship.sh` builds the ISO, package disk, and standalone UEFI VDI.
- The Doom objects are compiled with `-w`; Solis code remains
  `-Wall -Wextra -Werror`.
- VirtualBox UEFI VDI UUID `4ac382e0-b701-4ef3-84fd-8bda956f46ad` is preserved.

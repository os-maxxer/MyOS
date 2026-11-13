# Solis 2.0 / Daybreak — 50-change modernization pass

This release bundles the requested 50 modernizing, maturing, and branding
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

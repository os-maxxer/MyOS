# Solis modernization pass

The request was for 1,000 changes. This is not a padded checklist of 1,000
line items: it is the real change set, grouped by area, with the tests that
were actually run. The pass touches the desktop shell, the file manager and
the small platform pieces they need, and rebuilds every packaged app.

## Desktop shell (`gui/gui.c`)

- New surface palette: `C_SURFACE`, `C_SURFACE_HI`, `C_STROKE`,
  `C_ACCENT_SOFT`, `C_MUTED`, `C_TOAST`, used consistently across panels.
- Title bar is 32 px tall with the app icon, title and a focus underline.
- Minimize, maximize and close buttons have hover states; close turns red.
- Window frames use rounded cards, a soft shadow and a visible stroke.
- Start menu rebuilt as a 360 px card with an account avatar/initial and an
  edition badge.
- Start menu search field with a blinking caret, live filtering, category
  sections and a "No apps match" state.
- Enter launches the first match, Escape clears then closes, Backspace edits.
- Quick settings card: network/sound status, nine theme swatches, All
  settings, Log out, build id and shortcut hints.
- Toast notifications: four-card queue, auto-expire, hover pause and
  click-to-dismiss, exposed to apps through `gui_notify`.
- Desktop right-click menu: Open Terminal, Files, Settings, Next wallpaper,
  About Solis, Refresh desktop.
- About Solis modal: mark, release line, tagline, build id, kernel, session
  and edition.
- Snap preview overlay with labelled maximize/half/quarter zones; releasing
  the drag applies the highlighted zone.
- Resize from all four edges and four corners, with the correct drag offset.
- Alt+Tab window switcher with app icons and titles; hides itself after a
  short idle period.
- Tray: network bars, volume icon, guest pill, clock and date; the sliver at
  the dock's right edge shows the desktop.
- Desktop icons: hover/selection tiles and double-click launch.
- Keyboard shortcuts: Ctrl+Alt+T terminal, Ctrl+Alt+L logout, Alt+F4 closes
  the focused window.
- Menu repaint fix: filtering shrinks the start menu, so the damage region
  now covers the whole menu column instead of only the new rect.
- Windows honour each app's `get_size`, so the Files window opens at 800x480.
- New windows are centred when an app provides a preferred size.

## Files app (`apps/filebrowser.c`)

- Rewritten from the light 561-line browser to a dark, two-pane manager.
- Toolbar: Up, Refresh, `+ New`, and a path field that becomes a search field.
- Places sidebar (Home, Documents, Downloads, Desktop, Computer) with icons,
  an active pill and a New folder action.
- Sortable Name/Size/Type columns with ascending/descending order.
- File sizes are cached once per refresh instead of reopening every file
  during every comparison.
- Search filters names as you type; Enter opens the first result.
- Double-click opens folders and launches files in Editor, Browser or Notes
  based on the extension.
- Details inspector with type, size, text preview, empty-file and
  no-selection states.
- New-folder modal with a blinking caret and a success/failure toast.
- Delete confirmation modal instead of the old press-Delete-twice status.
- Right-click menu: Open, Open with (Notes/Editor/Browser), New folder,
  Properties, Delete.
- Properties modal with name, type, size and location.
- Status bar with item count, selection, size and current sort.
- Mouse-wheel scrolling and a proportional scrollbar; 800x480 default window.
- Painting and hit testing share one set of layout helpers, so click targets
  cannot drift from the drawn layout.

## Platform and packaging

- `gui_notify` syscall appended last to `struct syscall_table`, preserving
  the existing layout for already-built packages.
- `gui_close_focused()` used by Alt+F4 in the kernel loop.
- SPX app exports gain `handle_scroll` and `get_size`; Files registers both.
- Repository manifest sizes regenerated for all 14 packages; every `.spx`
  artifact rebuilt from this source.
- Settings keeps its dark eight-tab layout; boot/login and the CK3 map work
  from the same branch are included in this commit.

## Verification

- `make -j4`: passes (`MAKE_STATUS=0`), kernel, all SPX apps, ISO and disk.
- `make test-ck3`: 44 checks, 0 failures.
- Headless QEMU capture: first-boot installer, desktop, filtered start menu
  and the Files window all render; screenshots live in
  `$HOME/solis-test-artifacts/modern-shell3/`.
- Not exercised: VirtualBox, physical hardware, and every interaction in
  every app.

## Repository note

D: is a Ventoy exFAT USB that reported filesystem corruption after an
interrupted write. The corrupt `apps/filebrowser.c` entry was quarantined by
moving the old `apps` directory to `apps_bad/`, which is excluded through
`.git/info/exclude`; the project itself was rebuilt in a clean `apps/`.
Run `chkdsk D: /f` as administrator when convenient, then delete
`apps_bad/`.

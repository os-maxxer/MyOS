# Solis repository instructions

## Persistent test artifacts

`/tmp` is ephemeral and is wiped between WSL invocations and restarts. Never
store screenshots, QEMU captures, serial logs, monitor sockets, converted
images, or any other file needed across commands in `/tmp`.

Use `$HOME/solis-test-artifacts/` on the WSL user account instead:

```sh
ART="$HOME/solis-test-artifacts"
mkdir -p "$ART"
```

Requirements:

- Save every screenshot and QEMU test capture under `$ART`.
- Keep test outputs between WSL sessions and restarts.
- Before restarting WSL or cleaning temporary directories, preserve any
  artifact still needed for verification in `$ART`.
- Reuse existing artifacts instead of regenerating them unnecessarily; only
  recapture when the artifact is missing or stale.
- Delete artifacts only when explicitly instructed or when they are confirmed
  disposable.

Builds may live in a persistent directory such as `$HOME/solis-tick/` or the
project's own `build/` directory, but test evidence must go to
`$HOME/solis-test-artifacts/`.

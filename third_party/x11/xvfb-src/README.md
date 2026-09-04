# Standalone Xvfb source tree (plain make, no meson/ninja)

This tree contains **only** the source files needed to build an Xvfb server,
extracted from xserver 21.1.24. The directory layout mirrors the original
xserver tree so all `#include "…"` / `../`-style includes work unchanged.

The full Xorg source tree is **not** vendored in this repo — this snapshot
is the canonical build input. The only surviving trace of the upstream tree
is the FNX-local meson patch at `../xserver-fnx.patch` (needed only if you
ever re-extract from a fresh upstream fetch; see Regenerating).

## Contents

- `Makefile` — the whole build: per-file explicit rules, per-module static
  archives (`dix`, `fb`, `mi`, `os`, `xkb`, …), one final link. Compile
  flags mirror what the xserver meson build used.
- `include/` — the server's public/internal headers **plus the generated
  config headers** meson normally synthesizes from `.h.in`:
  `dix-config.h`, `xkb-config.h`, `xorg-config.h`, `version-config.h`,
  `xorg-server.h`, `xwin-config.h`.
- The `.c` sources per directory. `sources.txt` is the authoritative source
  list (265 files) as extracted.

## Building

```sh
make -C third_party/x11/xvfb-src \
     OUT="$PWD/.build/x11/xvfb" \
     CC="$PWD/tools/musl-gcc64.sh" -j8
# or from the repo root:  make xvfb64
```

External (non-Xorg) dependencies are **not** vendored here; they come from
the static-musl prefix at `.build/x11-prefix` (`pixman`, `libxkbfile`,
`libXfont2`, `libsha1`, `libXau`, `libXdmcp`) — build them first with
`tools/x11-deps-build.sh`. Override with `X11PREFIX=`.

Outputs land under `$(OUT)` (default `build/` inside this dir); the repo
target sends them to `.build/x11/xvfb`.

## Regenerating (only needed for an xserver upgrade)

1. Fetch upstream: `git clone https://gitlab.freedesktop.org/xorg/xserver`
   and check out tag `xorg-server-21.1.24` (or newer — expect drift).
2. Apply the FNX meson patches: `git apply ../xserver-fnx.patch`.
3. Meson-configure an xvfb-only build (options recorded in
   `docs/x11-xvfb-fb-plan.md`) so `.build/x11/xserver/build.ninja` exists.
4. Run `tools/x11-extract-xvfb.py`, which rebuilds this tree from that
   source + build graph, then re-commit the snapshot.

The per-archive source lists in the Makefile are derived from the build's
`build.ninja` — archive granularity matters because some modules
(`xi`/`xi_stubs`, `xkb`/`xkb_stubs`) only link correctly as archives with
`--start-group`, not as one flat object list.

Version note: sources + configs correspond to xserver 21.1.24 with the FNX
local meson patches applied.

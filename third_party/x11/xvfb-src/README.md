# Standalone Xvfb source tree (plain make, no meson/ninja)

This tree contains **only** the source files needed to build an Xvfb server,
extracted from the xserver 21.1.24 tree at `../xserver/`. The directory
layout mirrors the original so all `#include "…"` / `../`-style includes
work unchanged.

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

## Regenerating

`tools/x11-extract-xvfb.py` rebuilds this tree from `../xserver/`
(a working meson build at `.build/x11/xserver` supplies the exact object
graph + generated config headers). The per-archive source lists in the
Makefile were derived from that build's `build.ninja` — archive granularity
matters because some modules (`xi`/`xi_stubs`, `xkb`/`xkb_stubs`) only link
correctly as archives with `--start-group`, not as one flat object list.

Version note: sources + configs correspond to xserver 21.1.24 with the FNX
local meson patches already applied (see `../xserver` git history).

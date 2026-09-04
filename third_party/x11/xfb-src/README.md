# Standalone Xfb source tree (plain make, no meson/ninja)

This tree is the FNX fork of Xvfb, renamed to **Xfb**. It contains only
the source files needed to build the server — no meson/ninja/autotools.
It is a mechanical copy of `../xvfb-src/` (which was extracted from
xserver 21.1.24): same files, same layout, DDX renamed `hw/vfb` →
`hw/xfb`, binary/output names `Xvfb` → `Xfb` (`Xfb_screen` shm files,
`Xfb mouse` / `Xfb keyboard` device names, xwd window-name string).
The directory layout mirrors the original xserver tree so all
`#include "…"` / `../`-style includes work unchanged.

Keep `../xvfb-src` as the pristine upstream build; diverge from it here.
To sync upstream fixes: apply them to `xvfb-src`, re-extract there, then
re-copy + re-rebrand (the rename is: dir `hw/vfb`→`hw/xfb`, text
`Xvfb`→`Xfb`, `xvfb`→`xfb`, `hw/vfb`→`hw/xfb`, `hw_vfb`→`hw_xfb` in
Makefile object names).

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
make -C third_party/x11/xfb-src \
     OUT="$PWD/.build/x11/xfb" \
     CC="$PWD/tools/musl-gcc64.sh" -j8
# or from the repo root:  make xfb64
```

External (non-Xorg) dependencies are **not** vendored here; they come from
the static-musl prefix at `.build/x11-prefix` (`pixman`, `libxkbfile`,
`libXfont2`, `libsha1`, `libXau`, `libXdmcp`) — build them first with
`tools/x11-deps-build.sh`. Override with `X11PREFIX=`.

Outputs land under `$(OUT)` (default `build/` inside this dir); the repo
target sends them to `.build/x11/xfb`.

## Upstream / regeneration background

The Xorg source tree is not vendored in this repo; `xvfb-src` is the
canonical Xvfb build input, and the FNX-local meson patch is archived at
`../xserver-fnx.patch`. An xserver upgrade means: fetch xorg-server from
freedesktop gitlab, apply that patch, meson-configure an xvfb-only build
(options recorded in `docs/x11-xvfb-fb-plan.md`), run
`tools/x11-extract-xvfb.py` to regenerate `xvfb-src`, then re-fork into
this tree as described above.

Version note: sources + configs correspond to xserver 21.1.24 with the
FNX-local meson patches applied.

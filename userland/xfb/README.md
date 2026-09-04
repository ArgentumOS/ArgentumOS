# Xfb — FNX's X server (native fork of the Xvfb core)

**Xfb is a native FNX userland component.** It is not a vendored upstream:
it is the FNX-maintained fork of Xvfb — a first-party userland program
(`/bin/Xfb` in the root image). This tree contains the whole buildable
server: ~260 files forked once from Xserver 21.1.24's Xvfb build, plus
the small FNX delta below. The pristine upstream is **not kept in the
tree** — the fork is the component, and a pristine baseline is
regenerated on demand when a sync is wanted (see "Upstream sync"
below).

The fork is mechanical: same files, same layout, DDX renamed `hw/vfb` →
`hw/xfb`, binary/output names `Xvfb` → `Xfb`. The directory layout
mirrors the original xserver tree so all `#include "…"` / `../`-style
includes work unchanged.

## The FNX delta (what is not pristine upstream)

FNX-authored files:

- `hw/xfb/fnxinput.c` — FNX input backend (/dev/kbd, /dev/psaux).
- `hw/xfb/configargs.c` — reads the `system.config.xfb` config domain
  (libconfig) and synthesizes default command-line options
  (docs/x11-xvfb-fb-plan.md).

Upstream files FNX has modified:

- `hw/xfb/InitOutput.c`, `hw/xfb/InitInput.c` — fb0 framebuffer output +
  FNX input wiring.
- `os/utils.c` — one hook: `xfb_config_args()` at the top of
  `ProcessCommandLine()`.
- `fb/fbfill.c`, `include/dix-config.h`, `include/xkb-config.h`,
  `Makefile` — FNX build and render config.

Everything else in this tree is byte-identical to the pristine Xvfb
sources.

## Upstream sync (regenerate a pristine baseline on demand)

An upstream upgrade means: fetch xorg-server from freedesktop gitlab,
apply `third_party/x11/xserver-fnx.patch` (the archived FNX-local meson
changes), meson-configure an xvfb-only build (options recorded in
`docs/x11-xvfb-fb-plan.md`), run `tools/x11-extract-xvfb.py` to
regenerate `third_party/x11/xvfb-src` (the pristine mirror), then
re-copy + re-rebrand into this tree (the rename is: dir `hw/vfb`→
`hw/xfb`, text `Xvfb`→`Xfb`, `xvfb`→`xfb`, `hw/vfb`→`hw/xfb`,
`hw_vfb`→`hw_xfb` in Makefile object names), and re-apply the delta
above. Diffing this tree against the regenerated `xvfb-src` shows
exactly the FNX delta. (Older pristine versions are also recoverable
from this repo's git history, which contains the pre-promotion
`third_party/x11/xvfb-src`.)

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
make -C userland/xfb \
     OUT="$PWD/.build/x11/xfb" \
     CC="$PWD/tools/musl-gcc64.sh" -j8
# or from the repo root:  make xfb64
```

External (non-Xorg) dependencies are **not** vendored here; they come from
the static-musl prefix at `.build/x11-prefix` (`pixman`, `libxkbfile`,
`libXfont2`, `libsha1`, `libXau`, `libXdmcp`) — build them first with
`tools/x11-deps-build.sh`. Override with `X11PREFIX=`. FNX libconfig
(`include/libconfig.h` + `userland/libconfig.c`) is compiled in for the
`system.config.xfb` config domain.

Outputs land under `$(OUT)` (default `build/` inside this dir); the repo
target sends them to `.build/x11/xfb`.

Version note: sources + configs correspond to xserver 21.1.24 with the
FNX-local meson patches applied.
